"""Actual reader error/lifetime paths with synthetic state; no Torch/model load.

These tests qualify cache retirement and input bounds, not transformer numeric
equivalence, package provenance, model performance or resource admission.
"""
import importlib.util
import io
from pathlib import Path
import sys
import threading
import types
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("offline_character_model",
    Path(__file__).resolve().parents[1] / "scripts/offline_character_model.py")
M = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = M
SPEC.loader.exec_module(M)


class Vector(list):
    def __getitem__(self, item):
        value = super().__getitem__(item)
        return Vector(value) if isinstance(item, slice) else value

    def cumsum(self, dim):
        total, values = 0.0, []
        for value in self:
            total += value
            values.append(total)
        return Vector(values)

    def __add__(self, value):
        return Vector(item + value for item in self)


class ReaderLifetimeTests(unittest.TestCase):
    def setUp(self):
        # Deliberately bypass only model loading. All public reader methods and
        # their actual validation/locking/error cleanup remain under test.
        self.model = M.CharacterModel.__new__(M.CharacterModel)
        self.model.index = {"x": 3, "y": 4}
        self.model._initialize_cache()
        self.seed()

    def seed(self):
        self.model._scope = ("old-field", "generation")
        self.model._prefix_key = (2, 3)
        self.model._prefix = ("synthetic-hidden", "synthetic-kv")
        self.model._tails = [("synthetic-private-tail",)]

    def assert_retired(self):
        self.assertIsNone(self.model._scope)
        self.assertIsNone(self.model._prefix_key)
        self.assertIsNone(self.model._prefix)
        self.assertEqual(self.model._tails, [])

    def test_missing_scope_argument_and_unknown_components_retire_history(self):
        with self.assertRaises(TypeError):
            self.model.score_cached("x", ["y"])
        self.assert_retired()
        invalid = [None, "", (), [], {}, ("field", None), ("field", ""),
                   ("field", False), ("field", -1), "\x00", "\ud800",
                   "x" * 1025, object()]
        for scope in invalid:
            with self.subTest(scope=repr(scope)):
                self.seed()
                with self.assertRaises((ValueError, UnicodeError)):
                    self.model.score_cached("x", ["y"], scope=scope)
                self.assert_retired()

    def test_invalid_context_candidate_and_container_paths_retire_history(self):
        cases = [(None, ["x"]), ("a\x00b", ["x"]), ("\ud800", ["x"]),
                 ("\ud800" + "x" * 100, ["x"]),
                 ("x" * (M.MAX_TEXT_SCALARS + 1), ["x"]),
                 ("x", [None]), ("x", ["\ud800"]), ("x", ["x\x00"]),
                 ("x", [""]), ("x", ["x" * 64]), ("x", []),
                 ("x", ["x"] * 10), ("x", "x"), ("x", None)]
        for method in (self.model.score_cached, self.model.score_full):
            for context, candidates in cases:
                with self.subTest(method=method.__name__, context=repr(context)[:40],
                                  candidates=repr(candidates)[:40]):
                    self.seed()
                    args = {"scope": "new-field"} if method.__name__ == "score_cached" else {}
                    with self.assertRaises((ValueError, TypeError, UnicodeError)):
                        method(context, candidates, **args)
                    self.assert_retired()

    def test_direct_encode_error_retires_cache_before_allocating_ids(self):
        for text in (None, "\ud800", "\x00", "x" * (M.MAX_TEXT_SCALARS + 1)):
            self.seed()
            with self.assertRaises((ValueError, UnicodeError)):
                self.model.encode(text)
            self.assert_retired()

    def test_mid_computation_failure_retires_new_prefix_and_old_tails(self):
        calls = []
        def fail_after_prefix(ids, cache=None):
            calls.append(tuple(ids))
            if len(calls) == 2:
                raise RuntimeError("synthetic forward failure")
            return Vector([0.0]), "synthetic-cache"
        self.model.forward = fail_after_prefix
        with self.assertRaisesRegex(RuntimeError, "forward failure"):
            self.model.score_cached("x", ["y"], scope="new-field")
        self.assertEqual(len(calls), 2)
        self.assert_retired()

    def test_full_scoring_failure_also_retires_cached_field(self):
        def fail(ids, cache=None):
            raise RuntimeError("synthetic forward failure")
        self.model.forward = fail
        with self.assertRaises(RuntimeError):
            self.model.score_full("x", ["y"])
        self.assert_retired()

    def test_prefix_ids_allocate_only_retained_suffix(self):
        seen = []
        original = self.model.encode
        def record(text):
            seen.append(len(text))
            return original(text)
        self.model.encode = record
        prefix, tails = self.model._prefix_tokens("x" * M.MAX_TEXT_SCALARS, ["y"])
        self.assertEqual((len(prefix), tails, seen), (63, [[4]], [1, 62]))

    def test_clear_serializes_with_scoring_and_cannot_be_undone_by_publish(self):
        entered, release = threading.Event(), threading.Event()
        clear_requested, cleared = threading.Event(), threading.Event()
        failures, outputs = [], []
        calls = []
        def forward(ids, cache=None):
            calls.append(tuple(ids))
            if len(calls) == 1:
                entered.set()
                if not release.wait(2):
                    raise RuntimeError("bounded synthetic barrier timed out")
            return Vector([0.0] * len(ids)), "synthetic-cache"
        self.model.forward = forward
        self.model.torch = types.SimpleNamespace(cat=lambda values, dim:
            Vector(item for value in values for item in value))
        self.model._logps = lambda hidden, ids: Vector([-1.0] * len(ids))
        def score():
            try:
                outputs.append(self.model.score_cached("x", ["y"], scope="new-field"))
            except BaseException as error:
                failures.append(error)
        def clear():
            clear_requested.set()
            self.model.clear()
            cleared.set()
        scoring = threading.Thread(target=score)
        clearing = threading.Thread(target=clear)
        scoring.start()
        try:
            self.assertTrue(entered.wait(2))
            self.assertFalse(self.model._cache_lock.acquire(blocking=False))
            clearing.start()
            self.assertTrue(clear_requested.wait(2))
            self.assertFalse(cleared.wait(0.05))
        finally:
            release.set()
            scoring.join(2)
            if clearing.ident is not None:
                clearing.join(2)
        self.assertFalse(scoring.is_alive() or clearing.is_alive())
        self.assertEqual(failures, [])
        self.assertEqual(outputs, [[-1.0]])
        self.assertTrue(cleared.is_set())
        self.assert_retired()

    def test_dependency_pin_checked_without_importing_torch(self):
        with mock.patch.object(M.package_metadata, "version", return_value="2.11.0+cpu"):
            self.assertEqual(M._validate_dependency_version("2.11.0+cpu"), "2.11.0+cpu")
            for version in (None, "", "2.10.0", "2.11.0+other"):
                with self.assertRaises(ValueError):
                    M._validate_dependency_version(version)

    def test_growing_model_file_is_read_with_explicit_byte_bound(self):
        stream = io.BytesIO(b"x" * (M.MODEL_BYTES + 2))
        fake_path = mock.Mock()
        fake_path.stat.return_value.st_size = M.MODEL_BYTES
        fake_path.open.return_value = stream
        with self.assertRaisesRegex(ValueError, "read bound"):
            M.inspect_pinned(fake_path)
        self.assertNotIn("torch", sys.modules)


if __name__ == "__main__":
    unittest.main()
