"""Model-free preparation/worker cohort consistency, using authored fixtures."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "scripts"), str(ROOT / "tests")]

from test_ranking_student_training import fixture
from test_ranking_student_handoff import prepared_fixture
from tools.offline.ranking_student.contracts import digest
from tools.offline.ranking_student.training_data import build_preparation, input_order_binding
from tools.offline.ranking_student.train import schedule


class CohortTests(unittest.TestCase):
    def test_sparse_65_row_cohort_is_blocked_before_ready(self):
        prepared = prepared_fixture(65, sparse_families=True)
        self.assertEqual(prepared["summary"]["status"], "DATA_BLOCKED")
        self.assertIn("SCHEDULED_COHORT_GROUP_COVERAGE", prepared["summary"]["blockers"])
        self.assertIsNone(prepared["training_order"])
        self.assertIsNone(prepared["summary"]["training_cohort"])

    def ready(self):
        bundle = fixture(72)
        bundle["lineage"]["origin"] = "real_rime_train"  # Synthetic declaration only.
        return build_preparation(**bundle)

    def test_ready_freezes_the_existing_64_row_selection(self):
        prepared = self.ready()
        order = schedule(prepared)
        self.assertEqual(len(order), 64)
        self.assertEqual(order, prepared["training_order"])
        self.assertEqual(prepared["summary"]["training_cohort"],
                         input_order_binding(prepared["rows"], prepared["ids"], order))
        self.assertEqual(prepared["summary"]["training_cohort_sha256"],
                         digest(prepared["summary"]["training_cohort"]))
        order.reverse()
        self.assertNotEqual(order, prepared["training_order"])

    def test_worker_rejects_order_and_receipt_changes(self):
        for change in ("order", "missing", "row_id", "request_digest", "candidate_bool", "unknown", "digest"):
            prepared = self.ready()
            cohort = prepared["summary"]["training_cohort"]
            if change == "order": prepared["training_order"].reverse()
            elif change == "missing": del prepared["summary"]["training_cohort"]
            elif change == "row_id": cohort[0]["row_id"] = "unknown-synthetic-row"
            elif change == "request_digest": cohort[0]["pool_sha256"] = "0" * 64
            elif change == "candidate_bool": cohort[0]["candidate_source_indices"][0] = False
            elif change == "unknown": cohort[0]["extra"] = "synthetic"
            else: prepared["summary"]["training_cohort_sha256"] = "0" * 64
            with self.subTest(change=change), self.assertRaises(ValueError):
                schedule(prepared)


def tearDownModule():
    if {"torch", "numpy", "onnxruntime"} & set(sys.modules):
        raise AssertionError("cohort tests imported a model runtime")


if __name__ == "__main__":
    unittest.main()
