"""Authored synthetic native/teacher receipts only; never imports a heavy runtime."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT/"scripts"))
from test_ranking_student_stdlib import case, replan, teachers
from offline_pool_evaluation import gate_and_request
from tools.offline.ranking_student.contracts import digest
from tools.offline.ranking_student.training_data import (EXCLUDED_COLLECTIONS, build_preparation,
    bridge_legacy_bundle, parse_json, prepare, read_pinned, validate_lineage)
from tools.offline.ranking_student.train import main, schedule, run, optimize_once
from tools.offline.ranking_student.training_window import authorize
from tools.offline.ranking_student.config import Config
from tools.offline.ranking_student.codec import Codec


def fixture(count=2):
    cases, ranks, records, members = [], [], [], []
    for i in range(count):
        native = case()
        native.update(id=f"synthetic-{i}", prefix=f"synthetic context {i}")
        replan(native)
        _, request = gate_and_request(native)
        old = dict(id=native["id"], split="train", heldout=False,
            admission_status="FOUR_RESPONSE_NATIVE_GOLD_ADMITTED", request=request,
            rank_target=[0., 1.], response_provenance_sha256="c"*64, native_pool_sha256="d"*64)
        member = dict(document_id=f"synthetic-doc-{i//8}", family_id=f"synthetic-family-{i//8}")
        if member not in members: members.append(member)
        records.append(dict(id=native["id"], **member, collection_id="authored_synthetic_test",
            label_row_sha256=digest(old), native_case_sha256=digest(native),
            native_pool_sha256="d"*64, admission_sha256="a"*64, teachers=teachers()))
        cases.append(native); ranks.append(old)
    admission = dict(schema="ziliu.offline-admission.v2", input_sha256="e"*64,
        collection_receipt_sha256="a"*64, privacy_receipt_sha256="b"*64,
        cases=[{k:c[k] for k in ("id", "ordinary_scope", "current_prefix", "production_requested", "context_quality")}
            | {"prefix_sha256":hashlib.sha256(c["prefix"].encode()).hexdigest()} for c in cases])
    lineage = dict(schema="ziliu.ranking-training-lineage.v1", purpose="optimizer_train_only",
        origin="synthetic_fixture", authorization_receipt_sha256="a"*64, split_audit_sha256="b"*64,
        excluded_collections=sorted(EXCLUDED_COLLECTIONS),
        exclusions=dict(document_ids=[], family_ids=[], request_sha256=[]),
        split_manifest=dict(train=members, development=[], confirmation=[]), records=records)
    return dict(cases=cases, admission=admission, labels=dict(rank_rows=ranks, language_anchors=[]),
                lineage=lineage, native_input_sha256="e"*64)


def repin(f):
    for record, case_row, label in zip(f["lineage"]["records"], f["cases"], f["labels"]["rank_rows"]):
        record.update(label_row_sha256=digest(label), native_case_sha256=digest(case_row))


def write_fixture(directory, f):
    refs = {}
    for name, obj in (("native_cases", f["cases"]), ("native_admission", f["admission"]),
                      ("labels", f["labels"]), ("lineage", f["lineage"])):
        if name == "native_admission": obj["input_sha256"] = refs["native_cases"]["sha256"]
        raw = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        (directory/(name+".json")).write_bytes(raw)
        refs[name] = dict(path=name+".json", sha256=hashlib.sha256(raw).hexdigest())
    raw = json.dumps(dict(schema="ziliu.ranking-training-input.v1", purpose="optimizer_train_only", artifacts=refs)).encode()
    path = directory/"manifest.json"; path.write_bytes(raw)
    return path, hashlib.sha256(raw).hexdigest()


class TrainingBridgeTests(unittest.TestCase):
    def test_explicit_k32_preserves_every_candidate_default_stays_nine(self):
        req = dict(prefix="synthetic", pinyin="ceshi", candidates=[
            dict(source_index=i, text=f"synthetic candidate {i}") for i in range(32)])
        self.assertEqual(Config().max_candidates, 9)
        with self.assertRaises(ValueError): Codec().plan([req])
        config = Config(max_candidates=32)
        plan = Codec(config=config).plan([req])
        self.assertEqual(config.parameter_count(), 10_126_849)
        self.assertEqual(plan["source_indices"], [list(range(32))])
        self.assertEqual(sum(plan["pool_mask"][0]), 32)
        self.assertEqual(len(plan["candidate_ids"][0]), 32)
        req["candidates"].append(dict(source_index=32, text="extra"))
        with self.assertRaises(ValueError): Codec(config=config).plan([req])

    def test_pipeline_qualification_strictly_four_synthetic_rows(self):
        p = build_preparation(**fixture(4))
        self.assertEqual(schedule(p, qualification=True), [0, 1, 2, 3])
        with self.assertRaises(ValueError): schedule(p)
        p["summary"]["origin"] = "real_rime_train"
        with self.assertRaises(ValueError): schedule(p, qualification=True)
        with self.assertRaises(ValueError): schedule(build_preparation(**fixture(5)), qualification=True)
        p = build_preparation(**fixture(4)); p["summary"]["max_source_tokens"] = 129
        with self.assertRaises(ValueError): schedule(p, qualification=True)

    def test_exact_native_identity_and_onehot_preserved(self):
        f = fixture(); before = copy.deepcopy(f)
        rows, ids = bridge_legacy_bundle(**f)
        self.assertEqual(ids, ["synthetic-0", "synthetic-1"])
        self.assertEqual(rows[0]["target"]["candidate_source_indices"], [0, 2])
        self.assertEqual(rows[0]["target"]["probabilities"], [0., 1.])
        self.assertEqual(f, before)

    def test_changed_context_pinyin_text_and_order_rejected(self):
        for key in ("prefix", "pinyin", "text", "order", "identity"):
            f = fixture(); req = f["labels"]["rank_rows"][0]["request"]
            # De-alias deliberately: fixture gate request has its own candidate dicts.
            req = copy.deepcopy(req); f["labels"]["rank_rows"][0]["request"] = req
            if key in ("prefix", "pinyin"): req[key] += "a"
            elif key == "text": req["candidates"][0]["text"] += "x"
            elif key == "identity": req["candidates"][1]["source_index"] = 1
            else: req["candidates"].reverse()
            repin(f)
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "context/pinyin"):
                bridge_legacy_bundle(**f)

    def test_missing_id_duplicate_and_no_silent_drop(self):
        for side in ("labels", "cases", "lineage"):
            f = fixture()
            (f["labels"]["rank_rows"] if side == "labels" else
             f["cases"] if side == "cases" else f["lineage"]["records"]).pop()
            with self.subTest(side=side), self.assertRaises(ValueError): bridge_legacy_bundle(**f)
        f = fixture(); f["labels"]["rank_rows"][1]["id"] = "synthetic-0"
        with self.assertRaises(ValueError): bridge_legacy_bundle(**f)

    def test_legacy_unknown_fields_refused(self):
        f = fixture(); f["labels"]["rank_rows"][0]["gold"] = "not allowed"; repin(f)
        with self.assertRaises(ValueError): bridge_legacy_bundle(**f)

    def test_dev_confirmation_and_qualification_not_train(self):
        for key, value in (("split", "dev"), ("split", "confirmation"), ("heldout", True),
                           ("admission_status", "QUALIFIED_EVALUATION_ONLY")):
            f = fixture(); f["labels"]["rank_rows"][0][key] = value; repin(f)
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): bridge_legacy_bundle(**f)

    def test_soft_distribution_not_claimed_from_hard_admission(self):
        f = fixture(); f["labels"]["rank_rows"][0]["rank_target"] = [.25, .75]; repin(f)
        with self.assertRaisesRegex(ValueError, "one-hot"): bridge_legacy_bundle(**f)

    def test_duplicate_text_conflicting_hard_choice_rejected(self):
        f = fixture(); c = f["cases"][0]
        c["candidates"][2]["text"] = c["candidates"][0]["text"]
        c["candidates"][2]["commit"] = c["candidates"][0]["text"]
        replan(c)
        f["labels"]["rank_rows"][0]["request"] = gate_and_request(c)[1]
        repin(f)
        with self.assertRaisesRegex(ValueError, "conflicting hard choices"): bridge_legacy_bundle(**f)

    def test_consumed_59_330_96_and_opaque_overlap_rejected(self):
        for collection in EXCLUDED_COLLECTIONS:
            f = fixture(); f["lineage"]["records"][0]["collection_id"] = collection
            with self.subTest(collection=collection), self.assertRaises(ValueError): bridge_legacy_bundle(**f)
        for key in ("document_ids", "family_ids", "request_sha256"):
            f = fixture(); r = f["lineage"]["records"][0]
            value = (digest(f["labels"]["rank_rows"][0]["request"]) if key == "request_sha256"
                     else r[key[:-1]])
            f["lineage"]["exclusions"][key] = [value]
            with self.subTest(key=key), self.assertRaises(ValueError): bridge_legacy_bundle(**f)

    def test_cross_split_family_rejected_without_label_read(self):
        f = fixture(); f["lineage"]["split_manifest"]["confirmation"] = [dict(document_id="other", family_id="synthetic-family-0")]
        with self.assertRaises(ValueError): validate_lineage(f["lineage"])

    def test_receipt_and_native_hash_tampering(self):
        for field in ("label_row_sha256", "native_case_sha256", "native_pool_sha256"):
            f = fixture(); f["lineage"]["records"][0][field] = "f"*64
            with self.subTest(field=field), self.assertRaises(ValueError): bridge_legacy_bundle(**f)

    def test_privacy_fixed_and_weak_refused(self):
        for key in ("ordinary_scope", "current_prefix", "production_requested", "context_quality", "fixed"):
            f = fixture(); c = f["cases"][0]
            if key == "fixed": c["candidates"][0]["source"].update(origin="dedicated_fixed", strong_protection=True)
            else: c[key] = "weak" if key == "context_quality" else key == "production_requested"
            replan(c); repin(f)
            with self.subTest(key=key), self.assertRaises(ValueError): bridge_legacy_bundle(**f)

    def test_synthetic_never_training_ready(self):
        prepared = build_preparation(**fixture(64))
        self.assertIn("SYNTHETIC_FIXTURE_ONLY", prepared["summary"]["blockers"])
        with self.assertRaises(ValueError): schedule(prepared)

    def test_once_only_schedule_deterministic_and_diversity(self):
        f = fixture(72); f["lineage"]["origin"] = "real_rime_train"  # Simulate declaration, no real data.
        prepared = build_preparation(**f)
        self.assertEqual(prepared["summary"]["status"], "READY_FOR_AUTHORIZED_WINDOW")
        order = schedule(prepared)
        self.assertEqual(len(order), 64); self.assertEqual(len(set(order)), 64)
        self.assertEqual(order, schedule(prepared))
        self.assertEqual(prepared["codec"].decode(prepared["codec"].encode("\U00020000")), "\U00020000")

    def test_vocabulary_not_built_from_anchor_or_label(self):
        f = fixture(); f["labels"]["language_anchors"] = [{"text":"\u9f98"}]
        p = build_preparation(**f)
        self.assertNotIn("\u9f98", p["characters"])
        self.assertEqual(p["summary"]["ignored_language_anchors"], 1)

    def test_pinned_io_and_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as temp:
            path, sha = write_fixture(Path(temp), fixture())
            self.assertEqual(prepare(path, sha)["summary"]["rows"], 2)
            with self.assertRaises(ValueError): prepare(path, "0"*64)
            with self.assertRaises(ValueError): read_pinned(path, sha, 4)
            (Path(temp)/"labels.json").write_text("{}")
            with self.assertRaises(ValueError): prepare(path, sha)
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}'):
            with self.assertRaises(ValueError): parse_json(raw)

    def test_prepare_cli_never_runtime(self):
        with tempfile.TemporaryDirectory() as temp:
            path, sha = write_fixture(Path(temp), fixture())
            out = Path(temp)/"preparation.json"
            self.assertEqual(main(["--manifest", str(path), "--manifest-sha256", sha, "--out", str(out)]), 2)
            self.assertFalse(json.loads(out.read_text())["runtime_loaded"])
        self.assertFalse({"torch", "numpy", "onnxruntime"} & sys.modules.keys())

    def test_run_without_window_fails_before_data_io(self):
        with patch.dict("os.environ", {}, clear=True), patch("tools.offline.ranking_student.train.prepare") as io:
            with self.assertRaises(PermissionError):
                main(["--run", "--manifest", "missing.json", "--manifest-sha256", "a"*64, "--out", "unused"])
            io.assert_not_called()
            with self.assertRaises(PermissionError): authorize(None, None, "a"*64)

    def test_expired_check_prevents_import_and_output_creation(self):
        f = fixture(64); f["lineage"]["origin"] = "real_rime_train"
        p = build_preparation(**f)
        def expired(): raise TimeoutError("synthetic expired window")
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)/"never-created"
            with self.assertRaises(TimeoutError): run(p, out, expired, {})
            self.assertFalse(out.exists())
        self.assertNotIn("torch", sys.modules)

    def test_synthetic_loop_updates_once_and_checks_expiry_before_step(self):
        prepared = build_preparation(**fixture())
        for failure in (None, "expiry", "missing_gradient", "nonfinite_gradient", "nonfinite_parameter"):
            model, optimizer = Mock(), Mock()
            model.parameters.return_value = [Mock(grad=None if failure == "missing_gradient" else 1)]
            model.tensors.return_value = {}
            model.objective.return_value.detach.return_value = .5
            model.finite_gradient_norm.return_value = .25
            if failure == "nonfinite_gradient": model.finite_gradient_norm.side_effect = ValueError("synthetic NaN")
            calls = []
            def check():
                calls.append(True)
                if failure == "expiry" and len(calls) == 2: raise TimeoutError("synthetic deadline")
            report = dict(optimizer_steps=0, history=[])
            if failure is None:
                optimize_once(model, optimizer, prepared, [0, 1], check, lambda _: True, report)
                self.assertEqual(optimizer.step.call_count, 2)
                self.assertEqual([x["row_id"] for x in report["history"]], ["synthetic-0", "synthetic-1"])
            else:
                with self.subTest(failure=failure), self.assertRaises((RuntimeError, ValueError, TimeoutError)):
                    optimize_once(model, optimizer, prepared, [0, 1], check,
                                  lambda _: failure != "nonfinite_parameter", report)
                self.assertEqual(optimizer.step.call_count, int(failure == "nonfinite_parameter"))
                self.assertEqual(report["optimizer_steps"], int(failure == "nonfinite_parameter"))


if __name__ == "__main__": unittest.main()
