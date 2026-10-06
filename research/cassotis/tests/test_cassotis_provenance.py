# SPDX-License-Identifier: GPL-3.0-only
import hashlib
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import audit_cassotis_assets as assets
import cassotis_runtime_provenance as runtime


class Provenance(unittest.TestCase):
    def test_loaded_runtime_requires_a_known_origin_inside_verified_vendor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wheel, vendor = root / "fixture.whl", root / "vendor"
            package = vendor / "onnxruntime"
            package.mkdir(parents=True)
            member = package / "__init__.py"
            content = b"# public synthetic fixture; never imported\n"
            with zipfile.ZipFile(wheel, "w") as archive:
                archive.writestr("onnxruntime/__init__.py", content)
            member.write_bytes(content)
            outside = root / "outside.py"
            outside.write_bytes(content)
            with patch.object(runtime, "ORT_WHEEL_SHA256", runtime.digest(wheel)):
                for origin in (None, str(outside)):
                    module = types.SimpleNamespace(__file__=origin)
                    with patch.dict(sys.modules, {"onnxruntime.synthetic": module}):
                        with self.assertRaises(ValueError):
                            runtime.verify_runtime(vendor, wheel)
                module = types.SimpleNamespace(__file__=str(member))
                with patch.dict(sys.modules, {"onnxruntime.synthetic": module}):
                    self.assertEqual(runtime.verify_runtime(vendor, wheel), vendor.resolve())

    def test_self_reported_commit_cannot_authenticate_forged_tree(self):
        forged = {"sha": assets.COMMIT, "truncated": False, "tree": [
            {"path": "LICENSE", "mode": "100644", "type": "blob", "sha": "0" * 40}]}
        with self.assertRaises(ValueError):
            assets.verify_tree_object(forged)

    def test_all_parts_required_even_when_combined_file_exists(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "upstream/data/models/char_lm"
            model.mkdir(parents=True)
            (model / "char_lm.onnx").write_bytes(b"combined fixture")
            (model / "char_lm.onnx.000").write_bytes(b"x")
            expected = {"bytes": 1, "sha256": hashlib.sha256(b"x").hexdigest()}
            with patch.dict(assets.PINNED_SOURCE_FILES, {
                    "data/models/char_lm/char_lm.onnx.000": expected,
                    "data/models/char_lm/char_lm.onnx.001": expected}, clear=True):
                with self.assertRaises(ValueError):
                    assets.require_asset_inventory(root)

    def test_runtime_code_tamper_and_bytecode_are_rejected_without_import(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wheel, vendor = root / "fixture.whl", root / "vendor"
            vendor.mkdir()
            package = vendor / "onnxruntime"
            package.mkdir()
            member = package / "__init__.py"
            content = b"# synthetic fixture; never imported\n"
            with zipfile.ZipFile(wheel, "w") as archive:
                archive.writestr("onnxruntime/__init__.py", content)
            member.write_bytes(content)
            with patch.object(runtime, "ORT_WHEEL_SHA256", runtime.digest(wheel)):
                runtime.verify_runtime(vendor, wheel)
                member.write_bytes(content + b"# changed\n")
                with self.assertRaises(ValueError):
                    runtime.verify_runtime(vendor, wheel)
                member.write_bytes(content)
                cached = package / "__pycache__"
                cached.mkdir()
                (cached / "__init__.cpython-313.pyc").write_bytes(b"unqualified cached code")
                with self.assertRaises(ValueError):
                    runtime.verify_runtime(vendor, wheel)


if __name__ == "__main__":
    unittest.main()
