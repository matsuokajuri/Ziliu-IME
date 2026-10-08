"""Synthetic Job API and source contracts only; no Job creation or model runtime."""
import copy
import ctypes
import hashlib
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "scripts"), str(ROOT / "tests")]

from offline_pool_evaluation import gate_and_request
from test_ranking_student_training import fixture, repin
from test_ranking_student_stdlib import replan
from tools.offline.ranking_student.codec import Codec
from tools.offline.ranking_student.config import Config
from tools.offline.ranking_student.training_data import bridge_legacy_bundle, validate_lineage
from tools.offline.ranking_student.training_window import windows_limits


def kernel_fixture(*, member=True, memory=2 * 1024**3, flags=None, affinity=3):
    kernel = Mock()
    kernel.GetCurrentProcess.return_value = 100
    kernel.OpenProcess.return_value = 200
    kernel.CloseHandle.return_value = True
    kernel.GetPriorityClass.return_value = 0x4000

    def duplicate(controller, source, process, result, access, inherit, options):
        result._obj.value = 300
        return True

    def membership(process, job, result):
        result._obj.value = member
        return True

    def limits(job, kind, result, size, returned):
        result._obj.basic.flags = (0x2000 | 0x200 | 0x10 | 0x20) if flags is None else flags
        result._obj.basic.affinity = 3
        result._obj.basic.priority = 0x4000
        result._obj.job_memory = memory
        return True

    def process_affinity(process, result, system):
        result._obj.value = affinity
        system._obj.value = 15
        return True

    kernel.DuplicateHandle.side_effect = duplicate
    kernel.IsProcessInJob.side_effect = membership
    kernel.QueryInformationJobObject.side_effect = limits
    kernel.GetProcessAffinityMask.side_effect = process_affinity
    return kernel


class ExactJobQueryTests(unittest.TestCase):
    def invoke(self, kernel):
        with patch.object(ctypes, "WinDLL", return_value=kernel, create=True):
            windows_limits(2 * 1024**3, 1234, 5678)

    def test_query_only_noninherited_duplicate_checks_exact_job_and_closes(self):
        kernel = kernel_fixture()
        self.invoke(kernel)
        kernel.OpenProcess.assert_called_once_with(0x40, False, 1234)
        args = kernel.DuplicateHandle.call_args.args
        self.assertEqual((args[0], args[1].value, args[2]), (200, 5678, 100))
        self.assertEqual(args[4:], (0x4, False, 0))
        self.assertEqual(kernel.IsProcessInJob.call_args.args[1].value, 300)
        self.assertEqual(kernel.QueryInformationJobObject.call_args.args[0].value, 300)
        self.assertEqual([call.args[0].value if hasattr(call.args[0], "value") else call.args[0]
                          for call in kernel.CloseHandle.call_args_list], [200, 300])

    def test_outside_exact_job_rejected_before_query_and_handle_closed(self):
        kernel = kernel_fixture(member=False)
        with self.assertRaises(PermissionError):
            self.invoke(kernel)
        kernel.QueryInformationJobObject.assert_not_called()
        self.assertEqual(kernel.CloseHandle.call_count, 2)

    def test_wrong_memory_affinity_or_breakaway_rejected(self):
        required = 0x2000 | 0x200 | 0x10 | 0x20
        for kwargs in ({"memory": 4 * 1024**3}, {"affinity": 7},
                       {"flags": required | 0x800}, {"flags": required | 0x1000},
                       {"flags": required & ~0x2000}):
            with self.subTest(kwargs=kwargs), self.assertRaises(PermissionError):
                self.invoke(kernel_fixture(**kwargs))


class AdditionalTrainIdentityTests(unittest.TestCase):
    def test_same_complete_request_under_different_ids_rejected(self):
        f = fixture()
        f["cases"][1]["prefix"] = f["cases"][0]["prefix"]
        replan(f["cases"][1])
        f["labels"]["rank_rows"][1]["request"] = gate_and_request(f["cases"][1])[1]
        f["admission"]["cases"][1]["prefix_sha256"] = hashlib.sha256(
            f["cases"][1]["prefix"].encode()).hexdigest()
        repin(f)
        with self.assertRaisesRegex(ValueError, "duplicate or consumed exact training request"):
            bridge_legacy_bundle(**f)

    def test_document_overlap_rejected_even_with_different_family(self):
        f = fixture()
        f["lineage"]["split_manifest"]["development"] = [
            dict(document_id="synthetic-doc-0", family_id="different-family")]
        with self.assertRaisesRegex(ValueError, "crosses frozen split boundary"):
            validate_lineage(f["lineage"])

    def test_k32_full_utf8_fallback_and_batch_budget_preserve_all_text(self):
        codec = Codec(config=Config(max_candidates=32))
        request = dict(prefix=chr(0x20000) * 48, pinyin="a" * 64,
                       candidates=[dict(source_index=i, text=chr(0x20001 + i) * 63)
                                   for i in range(32)])
        plan = codec.plan([copy.deepcopy(request) for _ in range(8)])
        self.assertEqual(len(plan["source_ids"][0]), 259)
        self.assertEqual(len(plan["candidate_ids"][0][0]), 253)
        for row in range(8):
            self.assertEqual(plan["source_indices"][row], list(range(32)))
            self.assertTrue(all(plan["pool_mask"][row]))
            for candidate in range(32):
                self.assertEqual(codec.decode(plan["candidate_ids"][row][candidate][1:]),
                                 request["candidates"][candidate]["text"])
        with self.assertRaises(ValueError):
            codec.plan([request] * 9)
        request["candidates"][31]["text"] += chr(0x20000)
        with self.assertRaises(ValueError):
            codec.plan([request])


if __name__ == "__main__":
    unittest.main()
