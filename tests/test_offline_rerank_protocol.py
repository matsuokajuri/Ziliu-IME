"""Deterministic experiment boundaries; no corpus, model, optimizer or network."""
import importlib.util
from pathlib import Path
import sys
import unittest

SPEC = importlib.util.spec_from_file_location("offline_rerank_protocol",
    Path(__file__).resolve().parents[1] / "scripts/offline_rerank_protocol.py")
P = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = P
SPEC.loader.exec_module(P)


class OfflineRerankProtocolTests(unittest.TestCase):
    def test_two_xian_segmentations_are_not_filtered_by_character_count(self):
        facts = [P.CandidateFacts(True, False), P.CandidateFacts(True, False)]
        gate = P.safe_field_gate("下一站是", ["现", "西安"], facts)
        self.assertEqual(gate.indices, (0, 1))
        self.assertEqual(P.choose(gate, [-3.0, -2.0], 2), 1)

    def test_complete_key_leader_and_partial_candidates(self):
        facts = [P.CandidateFacts(True, False), P.CandidateFacts(False, False),
                 P.CandidateFacts(None, None), P.CandidateFacts(True, False)]
        self.assertEqual(P.reference_gate(facts).indices, (0, 3))
        self.assertEqual(P.choose(P.reference_gate(facts), [-1, 0], 4), 3)

    def test_unknown_dictionary_provenance_never_becomes_false(self):
        facts = [P.CandidateFacts(True, None), P.CandidateFacts(True, False)]
        self.assertEqual(P.reference_gate(facts).reason, "leader_dictionary_provenance_unknown")

    def test_trusted_leader_protected_but_nonleader_does_not_block(self):
        self.assertFalse(P.reference_gate([P.CandidateFacts(True, True)] * 2).indices)
        self.assertEqual(P.reference_gate([P.CandidateFacts(True, False),
            P.CandidateFacts(True, True)]).indices, (0, 1))

    def test_upstream_gate_and_extra_missing_prefix_guard_are_distinct(self):
        facts = [P.CandidateFacts(True, False)] * 2
        self.assertEqual(P.reference_gate(facts).indices, (0, 1))
        self.assertEqual(P.safe_field_gate("", ["甲", "乙"], facts).reason, "missing_field_prefix")

    def test_overlong_complete_text_is_declined_not_truncated(self):
        facts = [P.CandidateFacts(True, False)] * 2
        self.assertEqual(P.safe_field_gate("合成前文", ["甲", "乙" * 64], facts).reason,
                         "empty_or_overlong_complete_candidate")

    def test_nul_and_invalid_unicode_are_rejected(self):
        for text in ("a\x00b", "\ud800"):
            with self.assertRaises((ValueError, UnicodeError)):
                P.safe_field_gate(text, ["甲"], [P.CandidateFacts(True, False)])

    def test_comparison_cap_and_ties_preserve_original_order(self):
        gate = P.reference_gate([P.CandidateFacts(True, False)] * 12)
        self.assertEqual(gate.indices, tuple(range(9)))
        self.assertIsNone(P.choose(gate, [0.0] * 9, 12))
        for scores in ([0.0], [float("nan")] * 9):
            with self.assertRaises(ValueError):
                P.choose(gate, scores, 12)

    def test_gain_can_change_order_and_is_not_conditional_probability(self):
        self.assertEqual(P.conditional_gain([-2, -3], [-1, -4]), [-1, 1])
        with self.assertRaises(ValueError):
            P.conditional_gain([0], [])

    def test_alias_and_ambiguity_atom_leakage_are_rejected(self):
        base = {"id": "a", "split": "train", "family": "f1", "input": "syntheticcanonical",
                "ambiguity_atoms": ["fixture-left/fixture-right"], "canonical_inputs": ["syntheticcanonical"]}
        for change in ({"input": "syntheticcanonical", "family": "f2", "ambiguity_atoms": ["other"]},
                       {"input": "alias", "family": "f2"}):
            other = dict(base, id="b", split="test", **change)
            with self.assertRaises(ValueError):
                P.assert_family_isolation([base, other])

    def test_recall_misses_are_kept_and_breakages_are_separate(self):
        rows = [
            {"candidates": ["甲", "乙"], "expected": ["乙"], "promotion": 1},
            {"candidates": ["甲", "乙"], "expected": ["甲"], "promotion": 1},
            {"candidates": ["甲"], "expected": ["缺失"], "promotion": None},
            {"candidates": ["甲", "乙"], "expected": [], "promotion": 1, "protected": True},
            {"candidates": [], "expected": ["缺失"], "promotion": None},
        ]
        result = P.metrics(rows)
        self.assertEqual(result["targeted"], 4)
        self.assertEqual(result["recall_counts"]["200"], 2)
        self.assertEqual((result["corrections"], result["breakages"]), (1, 1))
        self.assertEqual((result["abstention_violations"], result["protected_violations"]), (1, 1))
        self.assertEqual(result["empty_pool"], 1)

    def test_actual_alias_cannot_overlap_another_splits_canonical_input(self):
        train = {"id": "a", "split": "train", "family": "f1", "input": "alias",
                 "ambiguity_atoms": ["a/b"], "canonical_inputs": ["syntheticcanonical"]}
        test = {"id": "b", "split": "test", "family": "f2", "input": "other",
                "ambiguity_atoms": ["c/d"], "canonical_inputs": ["alias"]}
        for rows in ([train, test], [test, train]):
            with self.assertRaisesRegex(ValueError, "cross-split overlap: input"):
                P.assert_family_isolation(rows)


if __name__ == "__main__":
    unittest.main()
