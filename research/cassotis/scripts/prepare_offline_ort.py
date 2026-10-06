# SPDX-License-Identifier: GPL-3.0-only
"""Verify/extract an official CPU wheel into an isolated directory; no pip hooks."""
import argparse
import base64
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import zipfile

WHEEL = "onnxruntime-1.20.1-cp313-cp313-win_amd64.whl"
SHA256 = "d30367df7e70f1d9fc5a6a68106f5961686d39b54d3221f760085524e8d38e16"


def prepare(wheel: Path, destination: Path) -> dict:
    with wheel.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if wheel.name != WHEEL or actual != SHA256:
        raise ValueError("official PyPI CPU wheel pin mismatch")
    if destination.exists():
        raise ValueError("refuse to overlay an existing vendor directory")
    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or any(PurePosixPath(n).is_absolute() or ".." in PurePosixPath(n).parts
                                               or "\\" in n or ":" in n or n.endswith(".pth") for n in names):
            raise ValueError("unsafe wheel paths")
        if sum(info.file_size for info in archive.infolist()) > 128 * 1024 * 1024:
            raise ValueError("wheel extraction size limit")
        records = list(csv.reader(io.StringIO(archive.read("onnxruntime-1.20.1.dist-info/RECORD").decode())))
        if {row[0] for row in records} != {n for n in names if not n.endswith("/")}:
            raise ValueError("wheel RECORD inventory differs")
        for name, pin, count in records:
            if not pin:
                if name != "onnxruntime-1.20.1.dist-info/RECORD":
                    raise ValueError("unexpected unpinned wheel member")
                continue
            content = archive.read(name)
            expected = "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(content).digest()).decode().rstrip("=")
            if pin != expected or int(count) != len(content):
                raise ValueError("wheel RECORD hash mismatch")
        destination.mkdir(parents=True)
        for name in names:
            info = archive.getinfo(name)
            mode = info.external_attr >> 16
            if mode & 0o170000 == 0o120000:
                raise ValueError("wheel symlink forbidden")
            path = destination.joinpath(*PurePosixPath(name).parts)
            if name.endswith("/"):
                path.mkdir(parents=True, exist_ok=True)
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("xb") as stream:
                stream.write(archive.read(name))
    return {"status": "OFFICIAL_CPU_WHEEL_EXTRACTED", "filename": WHEEL, "sha256": SHA256,
            "wheel_members": len(names), "license": "MIT", "wheel_code_executed": False,
            "model_loaded": False, "environment_installed": False,
            "scope": "Core inference uses existing NumPy. Optional ORT tools dependencies are not installed.",
            "notices": ["onnxruntime/LICENSE", "onnxruntime/ThirdPartyNotices.txt"]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--wheel", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.wheel, args.destination)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(result["status"])


if __name__ == "__main__":
    main()
