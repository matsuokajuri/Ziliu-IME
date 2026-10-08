"""Opt-in, model-free acceptance checks for two previously reported repairs.

The public 6c57f154 baseline fails two checks. Keep these outside the existing
stdlib/offline/training discovery patterns until the private repair is reviewed.
No run(), authorization override, model runtime, optimizer or real Job is used.
"""
import ast
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "scripts"), str(ROOT / "tests")]

from test_ranking_student_training import fixture, write_fixture
from tools.offline.ranking_student.training_data import prepare
from tools.offline.ranking_student.train import schedule


def prepared_fixture(count, *, sparse_families=False, synthetic=False):
    bundle = fixture(count)
    # Simulate only the declaration, as the existing 72-row test does. These
    # authored strings/receipts never become real training data or permission.
    if not synthetic:
        bundle["lineage"]["origin"] = "real_rime_train"
    if sparse_families:
        members = []
        for index, record in enumerate(bundle["lineage"]["records"]):
            rare = 23 <= index < 30
            member = {
                "document_id": "synthetic-shared-doc" if rare else f"synthetic-doc-{index}",
                "family_id": f"synthetic-rare-family-{index}" if rare else "synthetic-common-family",
            }
            record.update(member)
            if member not in members:
                members.append(member)
        bundle["lineage"]["split_manifest"]["train"] = members
    with tempfile.TemporaryDirectory(prefix="ziliu-handoff-") as directory:
        manifest, sha = write_fixture(Path(directory), bundle)
        return prepare(manifest, sha)


def report_order_writes(function):
    """Find actual writes to run()'s report, excluding nested helper bodies.

    This checks an explicit receipt field, not numerical model execution or the
    contents of an eventual runtime receipt. An equivalent helper/schema must
    be reviewed before adapting this deliberately narrow source check.
    """
    pending = list(function.body)
    writes = []
    while pending:
        node = pending.pop()
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
            continue
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if (isinstance(target, ast.Subscript)
                        and isinstance(target.value, ast.Name) and target.value.id == "report"
                        and isinstance(target.slice, ast.Constant)
                        and target.slice.value == "synthetic_input_order"):
                    writes.append(node.value)
                if isinstance(target, ast.Name) and target.id == "report":
                    if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name):
                        if node.value.func.id == "dict":
                            writes.extend(k.value for k in node.value.keywords
                                          if k.arg == "synthetic_input_order")
                    elif isinstance(node.value, ast.Dict):
                        writes.extend(value for key, value in zip(node.value.keys, node.value.values)
                                      if isinstance(key, ast.Constant) and key.value == "synthetic_input_order")
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name) and node.func.value.id == "report"
                and node.func.attr == "update"):
            writes.extend(k.value for k in node.keywords if k.arg == "synthetic_input_order")
        pending.extend(ast.iter_child_nodes(node))
    return writes


class PreparationAcceptanceTests(unittest.TestCase):
    def test_ready_output_has_a_consumable_64_row_cohort(self):
        # 65 is the smallest row count above the fixed 64-step cohort. Rows
        # 23..29 carry seven rare families in one document; 58 other rows have
        # distinct documents and share the eighth family. All requests are unique.
        try:
            prepared = prepared_fixture(65, sparse_families=True)
        except ValueError:
            return  # Rejecting this cohort before issuing READY is fail-closed.
        if prepared["summary"]["status"] != "READY_FOR_AUTHORIZED_WINDOW":
            self.assertEqual(prepared["summary"]["status"], "DATA_BLOCKED")
            self.assertTrue(prepared["summary"]["blockers"])
            return
        try:
            order = schedule(prepared)
        except ValueError as error:
            self.fail(f"prepare issued READY for 65 rows / 59 documents / 8 families, "
                      f"but its bounded schedule rejects that same preparation: {error}")
        self.assertEqual(len(order), 64)
        self.assertEqual(len(set(order)), 64)
        self.assertGreaterEqual(len({prepared["rows"][i]["document_id"] for i in order}), 8)
        self.assertGreaterEqual(len({prepared["rows"][i]["family_id"] for i in order}), 8)

    def test_standard_64_row_positive_control_is_schedulable(self):
        prepared = prepared_fixture(64)
        self.assertEqual(prepared["summary"]["status"], "READY_FOR_AUTHORIZED_WINDOW")
        self.assertEqual(set(schedule(prepared)), set(range(64)))

    def test_authored_synthetic_data_stays_blocked(self):
        prepared = prepared_fixture(65, sparse_families=True, synthetic=True)
        self.assertEqual(prepared["summary"]["status"], "DATA_BLOCKED")
        self.assertIn("SYNTHETIC_FIXTURE_ONLY", prepared["summary"]["blockers"])


class ReceiptSourceAcceptanceTests(unittest.TestCase):
    def test_report_has_explicit_synthetic_input_order_binding(self):
        path = ROOT / "tools/offline/ranking_student/train.py"
        module = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        function = next(node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "run")
        self.assertTrue(report_order_writes(function),
                        "run() emits ordered fixed_predictions_before/after/restored, but does not "
                        "write synthetic_input_order into their receipt. history.row_id and whole "
                        "manifest pins alone are not this explicit per-prediction binding.")
        # Presence is a minimal static gate only. On integration, inspect the
        # field's values against the same order[:4], row IDs and complete request
        # digests. This assertion cannot qualify a numerical/runtime receipt.


def tearDownModule():
    forbidden = {"torch", "numpy", "onnxruntime"} & set(sys.modules)
    if forbidden:
        raise AssertionError(f"model-free acceptance unexpectedly imported: {sorted(forbidden)}")


if __name__ == "__main__":
    unittest.main()
