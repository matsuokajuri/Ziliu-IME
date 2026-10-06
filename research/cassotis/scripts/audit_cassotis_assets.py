# SPDX-License-Identifier: GPL-3.0-only
"""Static pinned-asset checks. Does not import ONNX/ORT or execute model code."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import mmap
from pathlib import Path, PurePosixPath
import struct

from cassotis_asset_pins import GIT_TREE_SHA1, PINNED_SOURCE_FILES

COMMIT = "e4d632d20c296fc5f3dd1bfe3c74be6b60cafca2"
MODEL_SHA256 = "e8d583c5b941f1a9f7a9dcbcd3a83b196af7a6db6f3932c286382e95bd9b6cf6"
MODEL_BYTES = 177269274


def verify_tree_object(tree: dict) -> None:
    """Reconstruct the complete Git tree Merkle root, independently of self-report."""
    files, declared_trees, directories = {}, {}, {""}
    for entry in tree["tree"]:
        path = entry["path"]
        pure = PurePosixPath(path)
        if (pure.is_absolute() or ".." in pure.parts or "\\" in path or ":" in path
                or len(entry["sha"]) != 40 or any(c not in "0123456789abcdef" for c in entry["sha"])):
            raise ValueError("invalid Git tree path/hash")
        if entry["type"] == "tree":
            declared_trees[path] = entry["sha"]
            directories.add(path)
            continue
        if path in files or entry["mode"] not in ("100644", "100755", "120000", "160000"):
            raise ValueError("duplicate/unknown Git entry")
        files[path] = entry
        parent = pure.parent.as_posix()
        while parent != ".":
            directories.add(parent)
            parent = PurePosixPath(parent).parent.as_posix()
    computed = {}
    for directory in sorted(directories, key=lambda p: len(PurePosixPath(p).parts), reverse=True):
        children = []
        for path, entry in files.items():
            parent = PurePosixPath(path).parent.as_posix()
            if ("" if parent == "." else parent) == directory:
                name = PurePosixPath(path).name.encode("utf-8")
                children.append((name, entry["mode"].encode(), bytes.fromhex(entry["sha"])))
        for path, sha in computed.items():
            parent = PurePosixPath(path).parent.as_posix()
            if ("" if parent == "." else parent) == directory:
                name = PurePosixPath(path).name.encode("utf-8")
                children.append((name + b"/", b"40000", bytes.fromhex(sha)))
        payload = b"".join(mode + b" " + name.rstrip(b"/") + b"\0" + sha
                           for name, mode, sha in sorted(children, key=lambda row: row[0]))
        computed[directory] = hashlib.sha1(b"tree " + str(len(payload)).encode() + b"\0" + payload).hexdigest()
        if directory in declared_trees and computed[directory] != declared_trees[directory]:
            raise ValueError("declared subtree object differs")
    if computed.get("") != GIT_TREE_SHA1:
        raise ValueError("fixed Git tree Merkle root differs")


def require_asset_inventory(root: Path) -> None:
    """Required source/license/notice and four parts remain required after joining."""
    resolved = root.resolve(strict=True)
    for relative, expected in PINNED_SOURCE_FILES.items():
        path = root / "upstream" / relative
        if (not path.is_file() or path.is_symlink() or not path.resolve(strict=True).is_relative_to(resolved)
                or path.stat().st_size != expected["bytes"] or digest(path) != expected["sha256"]):
            raise ValueError("required pinned asset missing/changed: " + relative)


def digest(path: Path, algorithm: str = "sha256", *, git_blob: bool = False) -> str:
    value = hashlib.new(algorithm)
    if git_blob:
        value.update(b"blob " + str(path.stat().st_size).encode() + b"\0")
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def varint(data, start: int, end: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 70, 7):
        if start >= end:
            raise ValueError("truncated protobuf varint")
        byte = data[start]
        start += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, start
    raise ValueError("oversized protobuf varint")


def fields(data, start: int, end: int):
    while start < end:
        tag, start = varint(data, start, end)
        number, wire = tag >> 3, tag & 7
        if not number:
            raise ValueError("zero protobuf field")
        if wire == 0:
            value, start = varint(data, start, end)
            yield number, wire, value
            continue
        if wire == 2:
            count, start = varint(data, start, end)
        elif wire in (1, 5):
            count = 8 if wire == 1 else 4
        else:
            raise ValueError("unsupported protobuf wire type")
        stop = start + count
        if stop > end:
            raise ValueError("protobuf field outside message")
        yield number, wire, (start, stop)
        start = stop


def text_field(data, span) -> str:
    if span[1] - span[0] > 4096:
        raise ValueError("oversized protobuf name")
    return data[span[0]:span[1]].decode("utf-8", errors="strict")


def graph_metadata(path: Path) -> dict:
    """Read protobuf envelopes and skip raw weight bytes, with no deserialization."""
    counts = Counter()
    domains = Counter()
    initializers = []
    inputs, outputs, opsets = [], [], []
    ir = None
    graphs = 0
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        for number, wire, value in fields(data, 0, len(data)):
            if number == 1 and wire == 0:
                ir = value
            if number == 25:
                raise ValueError("local ONNX functions require a separate audit")
            if number == 8 and wire == 2:
                op = {}
                for n, w, v in fields(data, *value):
                    if n == 1 and w == 2:
                        op["domain"] = text_field(data, v)
                    if n == 2 and w == 0:
                        op["version"] = v
                opsets.append(op)
            if number != 7 or wire != 2:
                continue
            graphs += 1
            for n, w, v in fields(data, *value):
                if n in (11, 12) and w == 2:
                    name = next((text_field(data, q) for k, t, q in fields(data, *v)
                                 if k == 1 and t == 2), None)
                    (inputs if n == 11 else outputs).append(name)
                if n == 5 and w == 2:
                    item = {"dims": [], "raw_bytes": 0}
                    for k, t, q in fields(data, *v):
                        if k == 1:
                            if t == 0:
                                item["dims"].append(q)
                            elif t == 2:
                                pos = q[0]
                                while pos < q[1]:
                                    dim, pos = varint(data, pos, q[1])
                                    item["dims"].append(dim)
                        elif k == 2 and t == 0:
                            item["data_type"] = q
                        elif k == 8 and t == 2:
                            item["name"] = text_field(data, q)
                        elif k == 9 and t == 2:
                            item["raw_bytes"] = q[1] - q[0]
                        elif k == 13 or (k == 14 and q != 0):
                            raise ValueError("external tensor data is forbidden")
                    initializers.append(item)
                if n == 1 and w == 2:
                    op, domain = None, ""
                    for k, t, q in fields(data, *v):
                        if k == 4 and t == 2:
                            op = text_field(data, q)
                        if k == 7 and t == 2:
                            domain = text_field(data, q)
                        if k == 5 and t == 2:
                            for a, aw, av in fields(data, *q):
                                if a in (6, 11, 22, 23):
                                    raise ValueError("nested graphs/sparse attributes require separate audit")
                    if domain not in ("", "ai.onnx", "com.microsoft"):
                        raise ValueError("unapproved operator domain")
                    counts[op] += 1
                    domains[domain] += 1
    if graphs != 1 or not counts or inputs[:3] != ["ids", "positions", "attention_mask"]:
        raise ValueError("unexpected graph envelope")
    return {"ir_version": ir, "opsets": opsets, "operator_counts": dict(counts),
            "operator_domains": dict(domains), "inputs": inputs, "outputs": outputs,
            "initializers": initializers, "external_data": False, "local_functions": False}


def audit(root: Path, *, join: bool) -> dict:
    tree = json.loads((root / "upstream-tree.json").read_text(encoding="utf-8"))
    if tree["sha"] != COMMIT or tree["truncated"]:
        raise ValueError("pinned complete upstream tree required")
    verify_tree_object(tree)
    require_asset_inventory(root)
    table = {item["path"]: item for item in tree["tree"] if item["type"] == "blob"}
    files = {}
    for path in sorted((root / "upstream").rglob("*")):
        if not path.is_file() or path.name == "char_lm.onnx":
            continue
        relative = path.relative_to(root / "upstream").as_posix()
        expected = table[relative]
        git_hash = digest(path, "sha1", git_blob=True)
        if git_hash != expected["sha"] or path.stat().st_size != expected["size"]:
            raise ValueError("fixed tree blob mismatch: " + relative)
        files[relative] = {"bytes": path.stat().st_size, "git_blob_sha1": git_hash,
                           "sha256": digest(path)}
    directory = root / "upstream/data/models/char_lm"
    manifest = json.loads((directory / "runtime_manifest.json").read_text(encoding="utf-8"))
    if manifest["files"]["char_lm.onnx"] != MODEL_SHA256:
        raise ValueError("model manifest pin changed")
    for filename in ("char_lm_vocab.bin", "pinyin_readings.json"):
        if digest(directory / filename) != manifest["files"][filename]:
            raise ValueError("manifest asset digest mismatch")
    vocab = (directory / "char_lm_vocab.bin").read_bytes()
    if vocab[:8] != b"CASSLM01":
        raise ValueError("vocab magic")
    count, block, size = struct.unpack_from("<III", vocab, 8)
    points = struct.unpack_from("<" + "I" * count, vocab, 20)
    if (len(vocab) != 20 + count * 4 or count + 2 != size or size != manifest["vocab_size"]
            or block != manifest["block_size"] or len(set(points)) != count
            or any(cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF for cp in points)):
        raise ValueError("vocab bounds or manifest mismatch")
    model = directory / "char_lm.onnx"
    if join and not model.exists():
        pending = directory / "char_lm.onnx.joining"
        with pending.open("xb") as out:
            for part in range(4):
                with (directory / f"char_lm.onnx.{part:03}").open("rb") as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        out.write(chunk)
        if pending.stat().st_size != MODEL_BYTES or digest(pending) != MODEL_SHA256:
            raise ValueError("joined model mismatch (pending retained)")
        pending.rename(model)
    if not model.exists() or model.stat().st_size != MODEL_BYTES or digest(model) != MODEL_SHA256:
        raise ValueError("verified combined model required")
    metadata = graph_metadata(model)
    files["data/models/char_lm/char_lm.onnx"] = {"bytes": MODEL_BYTES, "sha256": MODEL_SHA256}
    return {"status": "PINNED_STATIC_AUDIT_PASS", "source_commit": COMMIT, "git_tree_sha1": GIT_TREE_SHA1, "files": files,
            "manifest": manifest, "vocabulary": {"count": count, "block": block, "size": size},
            "graph": metadata, "model_loaded": False, "model_executed": False,
            "limitations": ["Operator envelopes checked; this is not a general security proof.",
                            "Fixed Git object/HTTPS-origin consistency, not independent author/corpus-rights authentication.",
                            "Training checkpoint and enumerated corpus license evidence are unavailable.",
                            "No claim of production integration, accuracy or latency."]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--join", action="store_true")
    args = parser.parse_args()
    result = audit(args.root, join=args.join)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(json.dumps({key: result[key] for key in ("status", "source_commit", "model_loaded")}))


if __name__ == "__main__":
    main()
