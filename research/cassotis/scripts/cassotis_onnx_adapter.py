# SPDX-License-Identifier: GPL-3.0-only
"""Offline CPU scorer adapted from Cassotis' GPL-3.0 native tree scorer.

Reference: shenmin/cassotis-ime e4d632d, nc_char_lm_ort.inc (2026-10-06).
Modified implementation: strict asset pins, complete-pool batching and explicit
prefix cache. No dictionary, TSF, host field acquisition or production policy.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import sys
import threading
from typing import Hashable

from cassotis_runtime_provenance import verify_runtime

import numpy as np

COMMIT = "e4d632d20c296fc5f3dd1bfe3c74be6b60cafca2"
PINS = {
    "runtime_manifest.json": "55893c141cee71ccba9142ba332594067295cb29b21582ba28c1883ce454a85b",
    "char_lm.onnx": "e8d583c5b941f1a9f7a9dcbcd3a83b196af7a6db6f3932c286382e95bd9b6cf6",
    "char_lm_vocab.bin": "376c19b415024a2350083ca7c229bbe9f1d11191e562ef8ee174fd3afe7ec120",
}
MAX_NODES = 1024
MAX_TEXTS = 256


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_text(text: str) -> None:
    if not isinstance(text, str) or "\x00" in text:
        raise ValueError("NUL-free Unicode text required")
    text.encode("utf-8", errors="strict")


@dataclass(frozen=True)
class Vocabulary:
    tokens: dict[int, int]
    block_size: int
    size: int

    @classmethod
    def read(cls, path: Path):
        data = path.read_bytes()
        if len(data) < 20 or data[:8] != b"CASSLM01":
            raise ValueError("invalid CASSLM01 vocabulary")
        count, block, size = struct.unpack_from("<III", data, 8)
        if count + 2 != size or size != 12017 or block != 256 or len(data) != 20 + count * 4:
            raise ValueError("unexpected vocabulary header")
        points = struct.unpack_from("<" + "I" * count, data, 20)
        if len(set(points)) != count or any(cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF for cp in points):
            raise ValueError("invalid Unicode vocabulary")
        return cls({point: index + 2 for index, point in enumerate(points)}, block, size)

    def encode(self, text: str) -> list[int]:
        validate_text(text)
        return [self.tokens.get(ord(char), 1) for char in text]


@dataclass
class Trie:
    ids: list[int]
    positions: list[int]
    parents: list[int]
    paths: list[list[int]]
    root: int

    def mask(self, past_length: int = 0) -> np.ndarray:
        count = len(self.ids)
        mask = np.zeros((1, count, past_length + count), dtype=np.int32)
        mask[:, :, :past_length] = 1
        for index in range(count):
            node = index
            while node >= 0:
                mask[0, index, past_length + node] = 1
                node = self.parents[node]
        return mask


def pack(prefix: list[int], candidates: list[list[int]], *, cached_length: int = 0) -> Trie:
    """Pack ancestor-only branches in topological order; never inspect labels."""
    ids = list(prefix)
    parents = list(range(-1, len(prefix) - 1))
    positions = list(range(len(prefix)))
    root = len(prefix) - 1
    children = {}
    paths = []
    for tokens in candidates:
        node = root
        path = []
        for depth, token in enumerate(tokens):
            key = node, token
            if key not in children:
                children[key] = len(ids)
                ids.append(token)
                parents.append(node)
                positions.append(positions[node] + 1 if node >= 0 else cached_length + depth)
            node = children[key]
            path.append(node)
        paths.append(path)
    return Trie(ids, positions, parents, paths, root)


def chunks(prefix: list[int], candidates: list[list[int]], *, cached_length: int = 0):
    """Score every admitted candidate; no silent native max-nodes truncation."""
    begin = 0
    while begin < len(candidates):
        batch = []
        tree = None
        for tokens in candidates[begin:]:
            proposed = pack(prefix, [*batch, tokens], cached_length=cached_length)
            if len(proposed.ids) > MAX_NODES:
                break
            batch.append(tokens)
            tree = proposed
        if not batch or tree is None:
            raise ValueError("complete candidate exceeds node budget")
        yield begin, tree
        begin += len(batch)


class CassotisScorer:
    """One owned ORT CPU session, with one bounded context-prefix KV cache."""
    def __init__(self, directory: Path, *, runtime_directory: Path, runtime_wheel: Path,
                 threads: int = 2, timeout_ms: int = 2000, allow_experimental_cache: bool = False):
        self._owner_thread = threading.get_ident()
        self._allow_experimental_cache = allow_experimental_cache
        if not sys.dont_write_bytecode:
            raise ValueError("qualified owned runtime requires Python -B/PYTHONDONTWRITEBYTECODE=1")
        if not 1 <= threads <= 2 or not 1 <= timeout_ms <= 2000:
            raise ValueError("offline CPU/deadline budget")
        for filename, expected in PINS.items():
            if digest(directory / filename) != expected:
                raise ValueError("pinned asset mismatch: " + filename)
        self.manifest = json.loads((directory / "runtime_manifest.json").read_text(encoding="utf-8"))
        self.vocabulary = Vocabulary.read(directory / "char_lm_vocab.bin")
        verify_runtime(runtime_directory, runtime_wheel)
        sys.path.insert(0, str(runtime_directory.resolve(strict=True)))
        import onnxruntime as ort
        if ort.__version__ != "1.20.1":
            raise ValueError("audited ORT 1.20.1 required")
        ort.disable_telemetry_events()
        self.ort = ort
        self.timeout_ms = timeout_ms
        options = ort.SessionOptions()
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        options.intra_op_num_threads = threads
        options.inter_op_num_threads = 1
        options.add_session_config_entry("session.force_spinning_stop", "1")
        options.add_session_config_entry("session.use_device_allocator_for_initializers", "1")
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        self.session = ort.InferenceSession(str(directory / "char_lm.onnx"), sess_options=options,
                                           providers=["CPUExecutionProvider"])
        self.session.disable_fallback()
        inputs, outputs = self.session.get_inputs(), self.session.get_outputs()
        self.input_names, self.output_names = [i.name for i in inputs], [o.name for o in outputs]
        if (self.input_names != ["ids", "positions", "attention_mask"] + [f"past_{i}" for i in range(11)]
                or self.output_names != ["logp_all"] + [f"present_{i}" for i in range(11)]
                or [i.type for i in inputs[:3]] != ["tensor(int64)", "tensor(int64)", "tensor(int32)"]
                or any(i.type != "tensor(float)" or i.shape != [2, "B", 16, "P", 64] for i in inputs[3:])
                or outputs[0].type != "tensor(float)" or outputs[0].shape[-1] != self.vocabulary.size):
            raise ValueError("unexpected pinned graph signature")
        self._busy = threading.Lock()
        self.clear()

    def encode(self, text: str) -> list[int]:
        return self.vocabulary.encode(text)

    def _check_owner(self):
        if threading.get_ident() != self._owner_thread:
            raise RuntimeError("cache/scoring maintenance must run on the session owner thread")

    def clear(self):
        self._check_owner()
        self._cache_key = None
        self._past = None
        self._root_logp = None

    def _run(self, tree: Trie, *, past=None, presents=False):
        past_length = 0 if past is None else past[0].shape[3]
        feed = {
            "ids": np.asarray([tree.ids], dtype=np.int64),
            "positions": np.asarray([tree.positions], dtype=np.int64),
            "attention_mask": tree.mask(past_length),
        }
        for index, name in enumerate(self.input_names[3:]):
            feed[name] = np.empty((2, 1, 16, 0, 64), dtype=np.float32) if past is None else past[index]
        options = self.ort.RunOptions()
        timer = threading.Timer(self.timeout_ms / 1000, lambda: setattr(options, "terminate", True))
        timer.daemon = True
        timer.start()
        try:
            values = self.session.run(self.output_names if presents else ["logp_all"], feed, options)
        finally:
            timer.cancel()
            timer.join()
        logp = values[0]
        if logp.shape != (1, len(tree.ids), self.vocabulary.size) or not np.isfinite(logp).all():
            raise ValueError("invalid graph log probabilities")
        return values

    def _prepare(self, context: str, texts: list[str]):
        if not isinstance(context, str) or len(context) > 4096:
            raise ValueError("raw context exceeds bounded offline budget")
        if not texts or len(texts) > MAX_TEXTS:
            raise ValueError("candidate pool must contain 1..256 complete texts")
        if any(not isinstance(text, str) or not text or len(text) > 255 for text in texts):
            raise ValueError("empty/overlong complete candidate; no candidate-tail truncation")
        encoded = [self.encode(text) for text in texts]
        if any(not tokens or len(tokens) + 1 > self.vocabulary.block_size for tokens in encoded):
            raise ValueError("empty/overlong complete candidate; no candidate-tail truncation")
        context_ids = self.encode(context)
        room = self.vocabulary.block_size - 1 - max(map(len, encoded))
        kept = context_ids[-room:] if room else []
        return [0, *kept], encoded

    def score_sums(self, context: str, texts: list[str], *, cached: bool = False,
                   scope: Hashable | None = None) -> list[float]:
        self._check_owner()
        if cached:
            if not self._allow_experimental_cache:
                self.clear()
                raise RuntimeError("quantized prefix cache equivalence is unqualified; diagnostic opt-in required")
            try:
                if scope is None or scope is False or scope == "" or scope == () or scope == 0:
                    raise ValueError("explicit owned scope required for cache")
                hash(scope)
            except (TypeError, ValueError):
                self.clear()
                raise ValueError("explicit hashable owned scope required for cache") from None
        if not self._busy.acquire(blocking=False):
            self.clear()
            raise RuntimeError("scorer busy")
        try:
            prefix, candidates = self._prepare(context, texts)
            if not cached:
                self.clear()
                result = []
                for _, tree in chunks(prefix, candidates):
                    logp = self._run(tree)[0][0]
                    result.extend(float(np.float32(sum(float(logp[tree.parents[node], tree.ids[node]])
                                                       for node in path))) for path in tree.paths)
                return result
            key = scope, tuple(prefix)
            if self._cache_key != key:
                self.clear()
                outputs = self._run(pack(prefix, []), presents=True)
                self._past = outputs[1:]
                self._root_logp = outputs[0][0, -1].copy()
                self._cache_key = key
            result = []
            for _, tree in chunks([], candidates, cached_length=len(prefix)):
                logp = self._run(tree, past=self._past)[0][0]
                result.extend(float(np.float32(sum(float(self._root_logp[tree.ids[node]])
                                                   if tree.parents[node] == -1 else
                                                   float(logp[tree.parents[node], tree.ids[node]])
                                                   for node in path))) for path in tree.paths)
            return result
        except BaseException:
            self.clear()
            raise
        finally:
            self._busy.release()

    def score_full(self, context: str, texts: list[str]) -> list[float]:
        sums = self.score_sums(context, texts)
        return [score / len(self.encode(text)) for score, text in zip(sums, texts)]

    def score_independent_sums(self, context: str, texts: list[str]) -> list[float]:
        """Uncached one-sequence-per-candidate reference, with a common pool prefix.

        This is a separate research objective/path and must not be mixed with
        native packed scores to select the better answer after inspecting labels.
        The whole-process harness remains responsible for the aggregate budget.
        """
        self._check_owner()
        if not self._busy.acquire(blocking=False):
            self.clear()
            raise RuntimeError("scorer busy")
        try:
            self.clear()
            prefix, candidates = self._prepare(context, texts)
            result = []
            for tokens in candidates:
                tree = pack(prefix, [tokens])
                logp = self._run(tree)[0][0]
                result.append(float(np.float32(sum(float(logp[tree.parents[node], tree.ids[node]])
                                                  for node in tree.paths[0]))))
            return result
        finally:
            self.clear()
            self._busy.release()

    def score_independent(self, context: str, texts: list[str]) -> list[float]:
        sums = self.score_independent_sums(context, texts)
        return [score / len(self.encode(text)) for score, text in zip(sums, texts)]

    def score_cached(self, context: str, texts: list[str], *, scope: Hashable) -> list[float]:
        sums = self.score_sums(context, texts, cached=True, scope=scope)
        return [score / len(self.encode(text)) for score, text in zip(sums, texts)]
