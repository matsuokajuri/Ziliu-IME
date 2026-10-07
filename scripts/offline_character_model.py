"""CPU-only research reader for the pinned chinese-ime-lm int8 safetensors.

Architecture/format reference (Apache-2.0, chinese-ime-lm contributors, 2026):
https://github.com/metasequoiaime/chinese-ime-lm/tree/f4a3fc007fba051695ae8300de917bb824d458ba/reference
This first-party PyTorch implementation is not the upstream Rust runtime and
must not be described as its measured latency or a bitwise Rust equivalence.
No pickle, remote code, downloads, implicit text persistence or TIP integration.
"""
from __future__ import annotations

import hashlib
from functools import wraps
from importlib import metadata as package_metadata
import json
import math
from pathlib import Path
import struct
import threading


MODEL_SHA256 = "86ac529510cb3b4968a5e6ade83ec8080f5b34a0a681e75c362accbbd387d1c1"
MODEL_BYTES = 4_486_280
MAX_TEXT_SCALARS = 16_384
QUALIFIED_TORCH_RELEASE = "2.11.0"
EXPECTED_CONFIG = {"vocab": 8192, "n_layer": 6, "n_head": 4,
                   "n_embd": 192, "context": 64, "dropout": 0.1}


def _cache_transaction(method):
    """Serialize each public operation and retire all state on any exception.

    The wrapper also catches argument-binding failures, including omitted scope.
    RLock permits internal encode/forward calls without allowing clear() to race
    with a partially computed prefix or a later publication of candidate tails.
    """
    @wraps(method)
    def guarded(self, *args, **kwargs):
        with self._cache_lock:
            try:
                return method(self, *args, **kwargs)
            except BaseException:
                self._clear_locked()
                raise
    return guarded


def _validate_scope(scope):
    # Avoid mutable tokens and user-defined equality that could alias lifetimes.
    parts = (scope,) if type(scope) is str else scope
    if type(parts) is not tuple or not 1 <= len(parts) <= 8:
        raise ValueError("nonempty immutable cache lifetime required")
    total = 0
    for part in parts:
        if type(part) is int and 0 <= part <= 2**64 - 1:
            continue
        if type(part) is not str or not part or "\x00" in part:
            raise ValueError("unknown or invalid cache lifetime component")
        total += len(part.encode("utf-8", errors="strict"))
        if total > 1024:
            raise ValueError("cache lifetime exceeds bound")
    return scope


def _validate_dependency_version(expected):
    """The owned worker explicitly supplies its admitted, exact package version.

    Version equality does not authenticate package origin or enforce Job limits;
    those remain worker admission obligations. No import/install occurs here.
    """
    if (type(expected) is not str or
            expected.split("+", 1)[0] != QUALIFIED_TORCH_RELEASE):
        raise ValueError("explicit qualified Torch package version required")
    if package_metadata.version("torch") != expected:
        raise ValueError("installed Torch package differs from worker admission")
    return expected


def _unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("duplicate safetensors/metadata JSON key")
        value[key] = item
    return value


def inspect_pinned(path: Path) -> tuple[dict, bytes]:
    if path.stat().st_size != MODEL_BYTES:
        raise ValueError("pinned model size differs")
    with path.open("rb") as stream:
        data = stream.read(MODEL_BYTES + 1)
    if len(data) != MODEL_BYTES:
        raise ValueError("pinned model changed or exceeds read bound")
    if hashlib.sha256(data).hexdigest() != MODEL_SHA256:
        raise ValueError("pinned model hash differs")
    header_len = struct.unpack("<Q", data[:8])[0]
    if not 2 <= header_len <= 262144 or 8 + header_len >= len(data):
        raise ValueError("unbounded model header")
    header = json.loads(data[8:8 + header_len], object_pairs_hook=_unique_object)
    metadata = header["__metadata__"]
    for key, expected in {"format": "chinese-ime-lm", "version": "1",
                          "precision": "int8", "tied_embeddings": "true",
                          "license": "Apache-2.0"}.items():
        if metadata.get(key) != expected:
            raise ValueError(f"unsupported metadata {key}")
    if not metadata.get("attribution"):
        raise ValueError("missing corpus attribution")
    if json.loads(metadata["config"], object_pairs_hook=_unique_object) != EXPECTED_CONFIG:
        raise ValueError("unsupported model architecture")
    vocab = json.loads(metadata["vocab"])
    if (len(vocab) != 8192 or vocab[:3] != ["<pad>", "<unk>", "<bos>"]
            or len(set(vocab)) != len(vocab)
            or any(not isinstance(word, str) or len(word) != 1 for word in vocab[3:])):
        raise ValueError("invalid character vocabulary")
    expected = {"tok.weight": [8192, 192], "tok.weight.scale": [8192],
                "pos.weight": [64, 192], "ln_f.weight": [192], "ln_f.bias": [192]}
    for layer in range(6):
        prefix = f"blocks.{layer}."
        for norm in ("ln1", "ln2"):
            for suffix in ("weight", "bias"):
                expected[prefix + norm + "." + suffix] = [192]
        for name, out, width in (("qkv", 576, 192), ("proj", 192, 192),
                                  ("fc", 768, 192), ("out", 192, 768)):
            expected[prefix + name + ".weight"] = [out, width]
            expected[prefix + name + ".weight.scale"] = [out]
            expected[prefix + name + ".bias"] = [out]
    if set(header) != set(expected) | {"__metadata__"}:
        raise ValueError("unexpected or missing tensor names")
    payload = data[8 + header_len:]
    intervals = []
    for name, shape in expected.items():
        entry = header[name]
        dtype = "I8" if len(shape) == 2 and name != "pos.weight" else "F32"
        if entry["shape"] != shape or entry["dtype"] != dtype:
            raise ValueError(f"unexpected tensor geometry/dtype: {name}")
        left, right = entry["data_offsets"]
        if (type(left) is not int or type(right) is not int or
                not 0 <= left < right <= len(payload) or
                right - left != math.prod(shape) * (1 if dtype == "I8" else 4)):
            raise ValueError("invalid tensor offsets")
        intervals.append((left, right))
    end = 0
    for left, right in sorted(intervals):
        if left != end:
            raise ValueError("tensor overlap or unexplained payload gap")
        end = right
    if end != len(payload):
        raise ValueError("unexpected payload tail")
    return header, payload


class CharacterModel:
    """Inference only, dequantized f32 CPU; no optimizer or fine tuning here."""
    def __init__(self, path: Path, *, expected_torch_version: str):
        admitted_version = _validate_dependency_version(expected_torch_version)
        header, payload = inspect_pinned(path)
        # Import an already available environment only when explicitly run.
        import torch
        import torch.nn.functional as functional
        if str(torch.__version__) != admitted_version:
            raise ValueError("imported Torch differs from worker admission")
        self.torch, self.functional = torch, functional
        if torch.cuda.is_initialized():
            raise RuntimeError("research baseline requires CPU-only process")
        torch.set_num_threads(2)
        torch.set_num_interop_threads(1)
        self.metadata = header["__metadata__"]
        vocab = json.loads(self.metadata["vocab"])
        self.index = {ch: i for i, ch in enumerate(vocab) if len(ch) == 1}
        raw = {}
        for name, entry in header.items():
            if name == "__metadata__":
                continue
            left, right = entry["data_offsets"]
            dtype = torch.int8 if entry["dtype"] == "I8" else torch.float32
            tensor = torch.frombuffer(bytearray(payload[left:right]), dtype=dtype).clone().reshape(entry["shape"])
            if dtype == torch.float32 and not torch.isfinite(tensor).all():
                raise ValueError("nonfinite float tensor")
            if name.endswith(".scale") and not (tensor > 0).all():
                raise ValueError("invalid quantization scale")
            raw[name] = tensor
        self.weights = {}
        for name, tensor in raw.items():
            if name.endswith(".scale"):
                continue
            self.weights[name] = (tensor.float() * raw[name + ".scale"][:, None]
                                  if tensor.dtype == torch.int8 else tensor)
        self.parameter_count = sum(t.numel() for t in self.weights.values())
        # The upstream Config.parameters() formula omits both LayerNorms in
        # each block: 6 * 4 * 192 = 4,608 values. Count the actual pinned file.
        if self.parameter_count != 4_254_720:
            raise ValueError("parameter count differs")
        self._initialize_cache()

    def _initialize_cache(self):
        self._cache_lock = threading.RLock()
        self._clear_locked()

    @_cache_transaction
    def encode(self, text: str) -> list[int]:
        if (type(text) is not str or len(text) > MAX_TEXT_SCALARS or
                "\x00" in text):
            raise ValueError("bounded NUL-free Unicode text required")
        text.encode("utf-8", errors="strict")
        return [self.index.get(ch, 1) for ch in text]

    def clear(self) -> None:
        """Wait for an in-flight operation, then retire its complete cache."""
        with self._cache_lock:
            self._clear_locked()

    def _clear_locked(self) -> None:
        self._scope = self._prefix_key = self._prefix = None
        self._tails = []

    def _prefix_tokens(self, context, candidates):
        if type(candidates) not in (list, tuple):
            raise ValueError("explicit candidate array required")
        candidates = tuple(candidates)
        if not 1 <= len(candidates) <= 9:
            raise ValueError("candidate pool must contain 1..9 texts")
        if any(type(text) is not str or not 1 <= len(text) <= 63 for text in candidates):
            raise ValueError("complete candidates must contain 1..63 Unicode scalars")
        tails = [self.encode(text) for text in candidates]
        room = 64 - max(map(len, tails)) - 1
        if (type(context) is not str or len(context) > MAX_TEXT_SCALARS or
                "\x00" in context):
            raise ValueError("bounded NUL-free Unicode context required")
        context.encode("utf-8", errors="strict")
        # Validate the bounded input, but allocate token IDs only for retained
        # suffix positions. Invalid characters outside the suffix still fail.
        prefix = [2] + (self.encode(context[-room:]) if room else [])
        return prefix, tails

    @_cache_transaction
    def forward(self, ids, cache=None):
        torch, f = self.torch, self.functional
        with torch.inference_mode():
            offset = 0 if cache is None else cache[0][0].shape[1]
            if not ids or offset + len(ids) > 64:
                raise ValueError("forward position-table bound exceeded")
            weights = self.weights
            x = weights["tok.weight"][ids] + weights["pos.weight"][offset:offset + len(ids)]
            next_cache = []
            allowed = torch.arange(offset + len(ids))[None, :] <= (offset + torch.arange(len(ids)))[:, None]
            for layer in range(6):
                prefix = f"blocks.{layer}."
                y = f.layer_norm(x, (192,), weights[prefix + "ln1.weight"], weights[prefix + "ln1.bias"], 1e-5)
                qkv = f.linear(y, weights[prefix + "qkv.weight"], weights[prefix + "qkv.bias"])
                q, k, v = [value.reshape(len(ids), 4, 48).transpose(0, 1) for value in qkv.chunk(3, dim=-1)]
                if cache is not None:
                    k = torch.cat((cache[layer][0], k), dim=1)
                    v = torch.cat((cache[layer][1], v), dim=1)
                attention = ((q @ k.transpose(-1, -2)) / math.sqrt(48)).masked_fill(~allowed, -torch.inf)
                attended = (attention.softmax(dim=-1) @ v).transpose(0, 1).reshape(len(ids), 192)
                x = x + f.linear(attended, weights[prefix + "proj.weight"], weights[prefix + "proj.bias"])
                y = f.layer_norm(x, (192,), weights[prefix + "ln2.weight"], weights[prefix + "ln2.bias"], 1e-5)
                hidden = f.gelu(f.linear(y, weights[prefix + "fc.weight"], weights[prefix + "fc.bias"]), approximate="none")
                x = x + f.linear(hidden, weights[prefix + "out.weight"], weights[prefix + "out.bias"])
                next_cache.append((k, v))
            normalized = f.layer_norm(x, (192,), weights["ln_f.weight"], weights["ln_f.bias"], 1e-5)
            return normalized, tuple(next_cache)

    def _logps(self, hidden, ids):
        with self.torch.inference_mode():
            logps = (hidden @ self.weights["tok.weight"].T).log_softmax(dim=-1)
            return logps[self.torch.arange(len(ids)), ids]

    @_cache_transaction
    def score_full(self, context, candidates):
        """Independent full recomputation used to qualify the incremental path."""
        prefix, tails = self._prefix_tokens(context, candidates)
        values = []
        for tail in tails:
            hidden, _cache = self.forward(prefix + tail)
            prediction = hidden[len(prefix) - 1:len(prefix) + len(tail) - 1]
            values.append(float(self._logps(prediction, tail).mean()))
        return values

    @_cache_transaction
    def score_cached(self, context, candidates, *, scope):
        """Bounded per-field cache plus candidate-prefix reuse; ephemeral only.

        The caller supplies a complete opaque experiment/field lifetime. Equal
        text across different lifetimes cannot reuse this state. Production
        field identity is not admitted by this offline caller-supplied token.
        """
        scope = _validate_scope(scope)
        if len(candidates) > 9:
            raise ValueError("cached candidate pool exceeds nine")
        prefix, tails = self._prefix_tokens(context, candidates)
        key = tuple(prefix)
        if self._scope != scope or self._prefix_key != key:
            self.clear()
            hidden, cache = self.forward(prefix)
            self._scope, self._prefix_key = scope, key
            self._prefix = (hidden[-1:], cache)
        next_tails, scores = [], []
        prefix_hidden, prefix_cache = self._prefix
        for tail in tails:
            agreed, best = 0, None
            for previous in self._tails:
                shared = 0
                for left, right in zip(tail, previous[0]):
                    if left != right:
                        break
                    shared += 1
                shared = min(shared, len(tail) - 1)
                if shared > agreed:
                    agreed, best = shared, previous
            if agreed:
                last = best[1][agreed - 1:agreed]
                cache = tuple((k[:, :len(prefix) + agreed], v[:, :len(prefix) + agreed])
                              for k, v in best[2])
                carried = best[3][agreed - 1]
            else:
                last, cache, carried = prefix_hidden, prefix_cache, 0.0
            remaining = tail[agreed:]
            hidden, cache = self.forward(remaining, cache)
            prediction = self.torch.cat((last, hidden[:-1]), dim=0)
            logps = self._logps(prediction, remaining)
            cumulative = logps.cumsum(dim=0) + carried
            if agreed:
                hidden = self.torch.cat((best[1][:agreed], hidden), dim=0)
                cumulative = self.torch.cat((best[3][:agreed], cumulative), dim=0)
            scores.append(float(cumulative[-1] / len(tail)))
            next_tails.append((tuple(tail), hidden, cache, cumulative))
        # Every cache hangs from this scope and its currently admitted prefix.
        # The protocol bounds the pool; reject rather than retain unbounded data.
        self._tails = next_tails
        return scores
