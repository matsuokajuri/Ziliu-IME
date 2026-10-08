"""Synthetic receipt metadata only; never runs the worker or a model runtime."""
import ast
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "scripts"), str(ROOT / "tests")]

from test_ranking_student_training import fixture
from tools.offline.ranking_student.contracts import digest
from tools.offline.ranking_student.training_data import build_preparation
from tools.offline.ranking_student.train import synthetic_prediction_order, validate_synthetic_prediction_receipt


class ReceiptBindingTests(unittest.TestCase):
    def setUp(self):
        self.prepared = build_preparation(**fixture(4))
        self.binding = [dict(row_id=ident, pool_sha256=digest(row["request"]),
                             candidate_source_indices=[c["source_index"] for c in row["request"]["candidates"]])
                        for ident, row in zip(self.prepared["ids"], self.prepared["rows"])]
        # This is authored metadata, not a runtime qualification claim.
        self.report = dict(status="SYNTHETIC_PIPELINE_QUALIFIED", purpose="synthetic_pipeline_qualification",
                           synthetic_input_order=self.binding, optimizer_steps=4,
                           history=[dict(row_id=ident) for ident in self.prepared["ids"]],
                           fixed_predictions_before=[[0., 0.]] * 4,
                           fixed_predictions_after=[[.25, -.25]] * 4,
                           fixed_predictions_restored=[[.25, -.25]] * 4,
                           restored_fixed_predictions_exact=True)

    def test_roundtrip_metadata_is_accepted_without_runtime(self):
        report = json.loads(json.dumps(self.report))
        self.assertEqual(validate_synthetic_prediction_receipt(report, self.prepared), [0, 1, 2, 3])
        self.assertEqual(synthetic_prediction_order(self.prepared, report["synthetic_input_order"]), [0, 1, 2, 3])

    def test_missing_partial_extra_swapped_and_duplicate_binding_is_rejected(self):
        for change in ("missing", "partial", "extra", "swap", "duplicate", "unknown_row", "unknown_field", "not_list"):
            report = copy.deepcopy(self.report)
            binding = report["synthetic_input_order"]
            if change == "missing": del report["synthetic_input_order"]
            elif change == "partial": binding.pop()
            elif change == "extra": binding.append(copy.deepcopy(binding[0]))
            elif change == "swap": binding[0], binding[1] = binding[1], binding[0]
            elif change == "duplicate": binding[1] = copy.deepcopy(binding[0])
            elif change == "unknown_row": binding[0]["row_id"] = "unknown-synthetic-row"
            elif change == "unknown_field": binding[0]["unused"] = "synthetic"
            else: report["synthetic_input_order"] = tuple(binding)
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_synthetic_prediction_receipt(report, self.prepared)

    def test_digest_and_candidate_identity_or_order_changes_are_rejected(self):
        for change in ("digest", "candidate_order", "candidate_id", "candidate_bool", "candidate_container"):
            report = copy.deepcopy(self.report)
            item = report["synthetic_input_order"][0]
            if change == "digest": item["pool_sha256"] = "0" * 64
            elif change == "candidate_order": item["candidate_source_indices"].reverse()
            elif change == "candidate_id": item["candidate_source_indices"][1] = 1
            elif change == "candidate_bool": item["candidate_source_indices"][0] = False
            else: item["candidate_source_indices"] = tuple(item["candidate_source_indices"])
            with self.subTest(change=change), self.assertRaises(ValueError):
                synthetic_prediction_order(self.prepared, report["synthetic_input_order"])

    def test_complete_request_changes_invalidate_old_binding_even_when_repinned(self):
        for change in ("prefix", "pinyin", "text", "candidate_order"):
            prepared = build_preparation(**fixture(4))
            row = prepared["rows"][0]
            request = row["request"]
            if change in ("prefix", "pinyin"): request[change] += "a"
            elif change == "text": request["candidates"][0]["text"] += "a"
            else: request["candidates"].reverse()
            row["pool_sha256"] = digest(request)
            with self.subTest(change=change), self.assertRaises(ValueError):
                synthetic_prediction_order(prepared, self.binding)

    def test_all_phases_require_complete_finite_vectors(self):
        for key in ("fixed_predictions_before", "fixed_predictions_after", "fixed_predictions_restored"):
            for value in (None, [], [[0.]] * 4, [[True, 0.]] * 4, [[float("nan"), 0.]] * 4):
                report = copy.deepcopy(self.report)
                report[key] = value
                with self.subTest(key=key, value=repr(value)), self.assertRaises(ValueError):
                    validate_synthetic_prediction_receipt(report, self.prepared)

    def test_restored_predictions_and_step_history_must_match_binding(self):
        for change in ("restored", "restored_flag", "history", "steps", "steps_bool", "purpose", "partial"):
            report = copy.deepcopy(self.report)
            if change == "restored": report["fixed_predictions_restored"][0] = [1., 2.]
            elif change == "restored_flag": report["restored_fixed_predictions_exact"] = 1
            elif change == "history": report["history"].reverse()
            elif change == "steps": report["optimizer_steps"] = 3
            elif change == "steps_bool": report["optimizer_steps"] = True
            elif change == "purpose": report["purpose"] = "optimizer_training"
            else: report["status"] = "PARTIAL"
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_synthetic_prediction_receipt(report, self.prepared)

    def test_worker_prediction_and_completion_paths_consume_binding(self):
        # Read-only call-path evidence; no invocation of run() or fake tensor libs.
        module = ast.parse((ROOT / "tools/offline/ranking_student/train.py").read_text(encoding="utf-8"))
        run = next(n for n in module.body if isinstance(n, ast.FunctionDef) and n.name == "run")
        predict = next(n for n in ast.walk(run) if isinstance(n, ast.FunctionDef) and n.name == "predict")
        consumer = next(n for n in module.body if isinstance(n, ast.FunctionDef) and n.name == "main")
        self.assertTrue(any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
                            and n.func.id == "synthetic_prediction_order" for n in ast.walk(predict)))
        for function in (run, consumer):
            self.assertTrue(any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
                                and n.func.id == "validate_synthetic_prediction_receipt" for n in ast.walk(function)))


def tearDownModule():
    if {"torch", "numpy", "onnxruntime"} & set(sys.modules):
        raise AssertionError("receipt metadata tests imported a model runtime")


if __name__ == "__main__":
    unittest.main()
