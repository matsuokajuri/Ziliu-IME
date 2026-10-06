"""Inference allowlist and negative leakage tests; no model or teacher calls."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest

SPEC = importlib.util.spec_from_file_location("offline_rerank_protocol",
    Path(__file__).resolve().parents[1] / "scripts/offline_rerank_protocol.py")
P = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = P
SPEC.loader.exec_module(P)


class RequestTests(unittest.TestCase):
    def setUp(self):
        self.row = {"input": "xy", "context": "synthetic prefix",
                    "id": "SECRET-ID", "family": "SECRET-FAMILY", "split": "test",
                    "ambiguity_atoms": ["SECRET-GOLD-A/SECRET-GOLD-B"],
                    "canonical_tokens": ["SECRET-TOKENS"],
                    "unknown_nested": {"gold": "SECRET-NESTED"}}
        self.pool = [{"source_index": 0, "text": "candidate one", "gold": "SECRET-CANDIDATE"},
                     {"source_index": 3, "text": "candidate two", "score": 100}]

    def test_only_prefix_pinyin_and_candidate_identity_are_serialized(self):
        value = P.scoring_request(self.row, self.pool)
        self.assertEqual(value, {"prefix": "synthetic prefix", "pinyin": "xy",
            "candidates": [{"source_index": 0, "text": "candidate one"},
                           {"source_index": 3, "text": "candidate two"}]})
        serialized = json.dumps(value)
        for forbidden in ("SECRET", "ambiguity_atoms", "canonical", "family", "split", "gold", "score"):
            self.assertNotIn(forbidden, serialized)

    def test_label_bearing_rows_are_rejected_even_if_the_field_looks_empty(self):
        for field in ("gold", "expected", "target", "label", "should_abstain"):
            with self.subTest(field=field):
                with self.assertRaisesRegex(ValueError, "label-bearing"):
                    P.scoring_request(dict(self.row, **{field: None}), self.pool)

    def test_conflicting_contexts_and_invalid_inputs_are_rejected(self):
        for changes in ({"prefix": "conflict"}, {"input": ""}, {"input": "a1"},
                        {"input": "XY"}, {"input": "x" * 65}, {"context": "\ud800"},
                        {"context": "x" * 16_385}):
            with self.assertRaises((ValueError, UnicodeError)):
                P.scoring_request(dict(self.row, **changes), self.pool)

    def test_mapping_duplicates_bool_indices_and_oversized_pools_are_rejected(self):
        for pool in ([], self.pool * 5, [{"source_index": True, "text": "x"}],
                     [self.pool[0], self.pool[0]], [{"source_index": 200, "text": "x"}],
                     [{"source_index": 0, "text": "x" * 64}]):
            with self.assertRaises(ValueError):
                P.scoring_request(self.row, pool)


if __name__ == "__main__":
    unittest.main()
