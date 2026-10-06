# SPDX-License-Identifier: GPL-3.0-only
"""Standard-library-only runtime verification, before vendor paths/imports."""
import hashlib
from pathlib import Path
import sys
import zipfile

ORT_WHEEL_SHA256 = "d30367df7e70f1d9fc5a6a68106f5961686d39b54d3221f760085524e8d38e16"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_runtime(directory: Path, wheel: Path) -> Path:
    """Verify every vendor member before making that directory importable."""
    if digest(wheel) != ORT_WHEEL_SHA256:
        raise ValueError("official CPU wheel provenance pin mismatch")
    root = directory.resolve(strict=True)
    with zipfile.ZipFile(wheel) as archive:
        names = [name for name in archive.namelist() if not name.endswith("/")]
        actual_names = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
        if actual_names != set(names):
            raise ValueError("runtime inventory differs from official wheel")
        for name in names:
            path = root / name
            if path.is_symlink() or not path.resolve(strict=True).is_relative_to(root):
                raise ValueError("runtime path escaped isolated root")
            if digest(path) != hashlib.sha256(archive.read(name)).hexdigest():
                raise ValueError("runtime member differs from pinned wheel: " + name)
    for name, module in tuple(sys.modules.items()):
        if name == "onnxruntime" or name.startswith("onnxruntime."):
            origin = getattr(module, "__file__", None)
            if not origin or not Path(origin).resolve(strict=True).is_relative_to(root):
                raise ValueError("already imported ONNX Runtime has unqualified provenance")
    return root
