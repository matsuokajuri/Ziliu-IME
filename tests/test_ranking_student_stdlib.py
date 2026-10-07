"""Synthetic fixtures only. Safe to run without PyTorch or a model window."""
import ast
import copy
import math
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"scripts"))
from tools.offline.ranking_student.config import Config, tiny_config
from tools.offline.ranking_student.codec import Codec, CLS
from tools.offline.ranking_student.contracts import (digest, validate_request, validate_training_row,
                                                    adapt_legacy_rank_row, validate_split_manifest)
from tools.offline.ranking_student.reference import objective_and_gradient, ranked_source_indices
from tools.offline.ranking_student.model import build_model, train_step
from tools.offline.ranking_student.bridge import score_offline_case
from offline_evaluation_contracts import context_plan
from offline_context_policy_v2 import research_gate, SourceEvidence


def request():
    return {"prefix":"合成会议讨论", "pinyin":"mingtianjihua", "candidates":[
        {"source_index":0,"text":"明天讨论计划"}, {"source_index":2,"text":"明天的计划"}]}


def teachers():
    return [{"id":ident,"revision":"synthetic-fixture-only","license_receipt_sha256":"a"*64}
            for ident in ("local:Qwen3.6-27B","local:Gemma4-31B")]


def row():
    req = request()
    return {"schema":"ziliu.direct-ranking-train.v1","split":"train","heldout":False,
            "purpose":"optimizer","request":req,"pool_sha256":digest(req),
            "document_id":"synthetic-document","family_id":"synthetic-family","admission_sha256":"b"*64,
            "target":{"kind":"teacher_distribution","status":"resolved","candidate_source_indices":[0,2],
                      "probabilities":[0.25,0.75],"acceptable_source_indices":[],"teachers":teachers(),
                      "response_provenance_sha256":"c"*64}}


def case():
    req = request()
    cs = []
    for i, value in enumerate((req["candidates"][0]["text"],"合成未核验项",req["candidates"][1]["text"])):
        known = i != 1
        cs.append(dict(source_index=i,text=value,answers_key=known,eligible=known,commit=value,
                       remaining_input="",remaining_preedit="",source=dict(origin="system_composed" if known else "unknown",
                       answers_key=known,literal_match=known,strong_protection=False)))
    c = dict(id="synthetic-case",input=req["pinyin"],prefix=req["prefix"],candidates=cs,
             ordinary_scope=True,current_prefix=True,production_requested=False,context_quality="ordinary",preprocessing={})
    return replan(c)


def replan(c):
    cs = c["candidates"][:9]
    gate = research_gate(c["prefix"],[x["text"] for x in cs],[SourceEvidence(**x["source"]) for x in cs],
                         ordinary_scope=c["ordinary_scope"],current_prefix=c["current_prefix"],production_requested=c["production_requested"])
    c["preprocessing"] = context_plan(c["prefix"],[cs[i] for i in gate.indices])
    return c


class ContractTests(unittest.TestCase):
    def test_exact_parameter_formula(self):
        self.assertEqual(Config().parameter_count(),10_126_849)
        self.assertEqual(Config().source_tokens,259)
        self.assertEqual(Config().candidate_tokens,253)

    def test_bounded_geometry(self):
        for kwargs in ({"width":257},{"heads":3},{"max_candidates":10},{"max_batch":9},{"vocab_size":259}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                Config(**kwargs)

    def test_model_payload_exact_allowlist(self):
        for key in ("gold","right_context","document_id","teacher","field_id"):
            req = request(); req[key] = "synthetic"
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_request(req)

    def test_candidate_payload_exact_allowlist(self):
        req = request(); req["candidates"][0]["target"] = True
        with self.assertRaises(ValueError): validate_request(req)

    def test_duplicate_and_outside_source_identity(self):
        for ident in (0,True,9,-1):
            req = request(); req["candidates"][1]["source_index"] = ident
            with self.subTest(ident=ident), self.assertRaises(ValueError): validate_request(req)

    def test_missing_native_rejected(self):
        req = request(); req["candidates"][0]["source_index"] = 1
        with self.assertRaises(ValueError): validate_request(req)

    def test_overlength_candidate_not_truncated(self):
        req = request(); req["candidates"][0]["text"] = "𠀀"*64
        with self.assertRaises(ValueError): Codec().plan([req])

    def test_unicode_surrogate_and_nul_rejected(self):
        for value in ("\ud800","a\x00b"):
            req = request(); req["candidates"][0]["text"] = value
            with self.subTest(value=repr(value)), self.assertRaises((ValueError,UnicodeError)):
                validate_request(req)

    def test_bad_pinyin_rejected(self):
        for value in ("NIHAO","你好","ni3hao3",""," ' "):
            req = request(); req["pinyin"] = value
            with self.subTest(value=value), self.assertRaises(ValueError): validate_request(req)

    def test_training_admission(self):
        self.assertIsNotNone(validate_training_row(row()))
        for key,value in (("split","development"),("split","confirmation"),("heldout",True),("heldout",0),("purpose","evaluation")):
            r = row(); r[key] = value
            with self.subTest(key=key,value=value), self.assertRaises(ValueError): validate_training_row(r)

    def test_pool_binding_and_order(self):
        r = row(); r["request"]["candidates"].reverse()
        with self.assertRaises(ValueError): validate_training_row(r)
        r["pool_sha256"] = digest(r["request"])
        with self.assertRaises(ValueError): validate_training_row(r)

    def test_no_cassotis_teacher(self):
        r = row(); r["target"]["teachers"][0]["id"] = "Cassotis"
        with self.assertRaises(ValueError): validate_training_row(r)

    def test_distribution_validation(self):
        for probabilities in ([0,0],[math.nan,1],[-1,2],[1], [True,0]):
            r = row(); r["target"]["probabilities"] = probabilities
            with self.subTest(probabilities=probabilities), self.assertRaises(ValueError): validate_training_row(r)

    def test_unresolved_targets_never_native_pseudo_labels(self):
        for status in ("no_answer","tie","abstain","disagreement","refusal"):
            r = row(); r["target"].update(status=status,probabilities=[])
            self.assertIsNone(validate_training_row(r))
            r["target"]["probabilities"] = [1,0]
            with self.assertRaises(ValueError): validate_training_row(r)

    def test_multiple_acceptable_targets(self):
        r = row(); r["target"].update(kind="acceptable_set",probabilities=[],teachers=[],acceptable_source_indices=[0,2])
        self.assertIsNotNone(validate_training_row(r))

    def test_identical_text_conflicting_acceptability_rejected_without_mutation(self):
        for acceptable in ([0],[2]):
            for reverse in (False,True):
                r = row()
                for c in r["request"]["candidates"]: c["text"] = "合成例"
                if reverse: r["request"]["candidates"].reverse()
                r["pool_sha256"] = digest(r["request"])
                r["target"].update(kind="acceptable_set",probabilities=[],teachers=[],
                                   candidate_source_indices=[c["source_index"] for c in r["request"]["candidates"]],
                                   acceptable_source_indices=acceptable)
                before = copy.deepcopy(r)
                with self.subTest(acceptable=acceptable,reverse=reverse):
                    with self.assertRaisesRegex(ValueError,"conflicting acceptability"):
                        validate_training_row(r)
                    self.assertEqual(r,before)

    def test_identical_text_consistent_acceptability_preserves_ids(self):
        for acceptable in ([0,2],[3]):
            r = row()
            for c in r["request"]["candidates"]: c["text"] = "合成例"
            r["request"]["candidates"].append({"source_index":3,"text":"另一合成例"})
            r["pool_sha256"] = digest(r["request"])
            r["target"].update(kind="acceptable_set",probabilities=[],teachers=[],
                               candidate_source_indices=[0,2,3],acceptable_source_indices=acceptable)
            before = copy.deepcopy(r)
            self.assertIsNotNone(validate_training_row(r))
            self.assertEqual(Codec().plan([r["request"]])["source_indices"],[[0,2,3]])
            self.assertEqual(r,before)

    def test_identical_text_teacher_distribution_not_rewritten(self):
        r = row()
        r["request"]["candidates"][1]["text"] = r["request"]["candidates"][0]["text"]
        r["pool_sha256"] = digest(r["request"])
        before = copy.deepcopy(r)
        self.assertIsNotNone(validate_training_row(r))
        self.assertEqual(r,before)

    def test_acceptability_text_groups_use_exact_unicode(self):
        r = row()
        r["request"]["candidates"][0]["text"] = "é"
        r["request"]["candidates"][1]["text"] = "e\u0301"
        r["pool_sha256"] = digest(r["request"])
        r["target"].update(kind="acceptable_set",probabilities=[],teachers=[],acceptable_source_indices=[0])
        self.assertIsNotNone(validate_training_row(r))

    def test_legacy_adapter_retains_provenance(self):
        r = row()
        legacy = dict(split="train",heldout=False,admission_status="FOUR_RESPONSE_NATIVE_GOLD_ADMITTED",
                      request=r["request"],rank_target=[.25,.75],response_provenance_sha256="c"*64,native_pool_sha256="d"*64)
        migrated, evidence = adapt_legacy_rank_row(legacy,document_id="synthetic",family_id="synthetic",
                                                   admission_sha256="b"*64,teachers=teachers())
        self.assertEqual(migrated["target"]["probabilities"],[.25,.75])
        self.assertEqual(evidence["legacy_native_pool_sha256"],"d"*64)
        legacy["admission_status"] = "QUALIFIED_EVALUATION_ONLY"
        with self.assertRaises(ValueError):
            adapt_legacy_rank_row(legacy,document_id="synthetic",family_id="synthetic",admission_sha256="b"*64,teachers=teachers())

    def test_document_and_family_split_isolation(self):
        manifest = {split:[{"document_id":split+"-synthetic-document","family_id":split+"-synthetic-family"}]
                    for split in ("train","development","confirmation")}
        self.assertTrue(validate_split_manifest(manifest))
        for field in ("document_id","family_id"):
            changed = copy.deepcopy(manifest)
            changed["confirmation"][0][field] = changed["train"][0][field]
            with self.subTest(field=field), self.assertRaises(ValueError): validate_split_manifest(changed)


class CodecTests(unittest.TestCase):
    def test_lossless_distinct_oov(self):
        codec = Codec("合成明天")
        for value in ("合成𠀀🙂","陌生的词","éé","𠀀𠀁"):
            self.assertEqual(codec.decode(codec.encode(value)),value)
        self.assertNotEqual(codec.encode("𠀀"),codec.encode("𠀁"))

    def test_token_budget_worst_case(self):
        req = request(); req.update(prefix="𠀀"*48,pinyin="a"*64)
        req["candidates"][0]["text"] = "🙂"*63
        plan = Codec().plan([req])
        self.assertEqual(len(plan["source_ids"][0]),259)
        self.assertEqual(len(plan["candidate_ids"][0][0]),253)

    def test_batch_shapes_and_masks(self):
        req2 = request(); req2["candidates"] = req2["candidates"][:1]
        plan = Codec().plan([request(),req2])
        self.assertEqual(plan["pool_mask"],[[True,True],[True,False]])
        self.assertEqual(plan["source_indices"],[[0,2],[0,-1]])
        self.assertEqual(plan["candidate_ids"][1][1][0],CLS)
        self.assertEqual(sum(plan["candidate_mask"][1][1]),1)
        for b in range(2):
            self.assertEqual(len(plan["source_ids"][b]),len(plan["source_mask"][b]))
            for k in range(2):
                self.assertEqual(len(plan["candidate_ids"][b][k]),len(plan["candidate_mask"][b][k]))

    def test_order_equivariance_plan_and_stable_tie(self):
        a = request(); b = copy.deepcopy(a); b["candidates"].reverse()
        pa,pb = Codec().plan([a]),Codec().plan([b])
        self.assertEqual(pa["source_ids"],pb["source_ids"])
        self.assertEqual(pa["candidate_ids"][0],list(reversed(pb["candidate_ids"][0])))
        self.assertEqual(ranked_source_indices([2,0],[1.,1.]),[0,2])

    def test_request_and_vocab_binding(self):
        a,b = request(),request(); b["prefix"] = "其他合成上下文"
        ca,cb = Codec("甲乙"),Codec("乙甲")
        self.assertNotEqual(ca.sha256,cb.sha256)
        self.assertNotEqual(ca.plan([a])["request_sha256"],ca.plan([b])["request_sha256"])
        self.assertEqual(ca.plan([a])["codec_sha256"],ca.sha256)
        with self.assertRaises(TypeError): ca.index["丙"] = 500

    def test_repeated_text_preserves_distinct_ids(self):
        req = request(); req["candidates"][1]["text"] = req["candidates"][0]["text"]
        self.assertEqual(Codec().plan([req])["source_indices"],[[0,2]])


class ObjectiveTests(unittest.TestCase):
    def test_analytic_gradient_against_finite_difference(self):
        for kind,target in (("teacher_distribution",[.2,.8,0]),("teacher_distribution",[.2,.7999999,0]),("acceptable_set",[0,2])):
            scores = [-.3,.8,.2]
            loss,gradient = objective_and_gradient(scores,target,kind=kind)
            self.assertTrue(math.isfinite(loss))
            for i in range(3):
                plus,minus = scores[:],scores[:]; plus[i]+=1e-5; minus[i]-=1e-5
                numeric = (objective_and_gradient(plus,target,kind=kind)[0]-objective_and_gradient(minus,target,kind=kind)[0])/2e-5
                self.assertAlmostEqual(gradient[i],numeric,places=8)
            self.assertAlmostEqual(sum(gradient),0,places=12)

    def test_all_acceptable_and_single_candidate_zero_loss(self):
        for scores,target in (([1.2,-.1],[0,1]),([2.],[0])):
            loss,gradient = objective_and_gradient(scores,target,kind="acceptable_set")
            self.assertAlmostEqual(loss,0,places=12)
            self.assertTrue(all(abs(g)<1e-12 for g in gradient))

    def test_extreme_logits_finite(self):
        value,gradient = objective_and_gradient([-1000,1000],[1.,0.])
        self.assertTrue(math.isfinite(value))
        self.assertTrue(all(math.isfinite(g) for g in gradient))

    def test_subnormal_teacher_probability_ratios(self):
        loss,gradient = objective_and_gradient([.1,.2,.3],[1.,1e-300,1e-300])
        self.assertTrue(math.isfinite(loss))
        self.assertTrue(all(math.isfinite(g) for g in gradient))

    def test_shift_invariant_and_descent_direction(self):
        scores = [.1,.2]
        loss,g = objective_and_gradient(scores,[0,1])
        improved,_ = objective_and_gradient([s-.1*x for s,x in zip(scores,g)],[0,1])
        self.assertLess(improved,loss)
        self.assertAlmostEqual(objective_and_gradient([s+10 for s in scores],[0,1])[0],loss,places=12)


class ProtectionTests(unittest.TestCase):
    def test_existing_gate_retains_holes_and_never_promotes(self):
        calls = []
        result = score_offline_case(case(),lambda req: calls.append(req) or [0.,1.])
        self.assertEqual(result["candidate_source_indices"],[0,2])
        self.assertEqual(result["diagnostic_order"],[2,0])
        self.assertIsNone(result["promotion"])
        self.assertFalse(result["production_enabled"])
        self.assertEqual(set(calls[0]),{"prefix","pinyin","candidates"})

    def test_privacy_stale_empty_production_and_quality_zero_calls(self):
        for key,value in (("ordinary_scope",False),("current_prefix",False),("prefix",""),
                          ("production_requested",True),("context_quality","weak"),("context_quality","unknown")):
            c = case(); c[key] = value; replan(c)
            with self.subTest(key=key,value=value):
                result = score_offline_case(c,lambda req: self.fail("scorer called for forbidden context"))
                self.assertIsNone(result["promotion"])
                self.assertEqual(result["rank_logits"],[])

    def test_userfixed_and_history_protection_zero_calls(self):
        for origin in ("dedicated_fixed","explicit_history","user_learning_preference_unresolved","unknown"):
            c = case(); c["candidates"][0]["source"]["origin"] = origin
            c["candidates"][0]["source"]["strong_protection"] = origin in ("dedicated_fixed","explicit_history")
            replan(c)
            score_offline_case(c,lambda req: self.fail("protected source was scored"))

    def test_nested_label_and_unknown_flags_rejected(self):
        c = case(); c["candidates"][0]["source"]["gold"] = True
        with self.assertRaises(ValueError): score_offline_case(c,lambda req: self.fail("leak"))
        c = case(); c["ordinary_scope"] = "true"
        with self.assertRaises(ValueError): score_offline_case(c,lambda req: self.fail("leak"))

    def test_nonfinite_and_partial_scores_rejected(self):
        for scores in ([math.nan,0.],[1.]):
            with self.assertRaises(ValueError): score_offline_case(case(),lambda req:scores)

    def test_no_heavy_import_or_model_allocation(self):
        code = "from tools.offline.ranking_student.model import build_model; import sys; assert not ({'torch','numpy','onnxruntime'} & set(sys.modules));\ntry: build_model()\nexcept PermissionError: pass\nelse: raise AssertionError('unguarded model')\nassert 'torch' not in sys.modules"
        subprocess.run([sys.executable,"-B","-W","error","-c",code],cwd=ROOT,check=True,capture_output=True,text=True)
        with self.assertRaises(PermissionError): train_step(None,None,[],None)

    def test_every_new_module_parses(self):
        for path in (ROOT/"tools/offline/ranking_student").glob("*.py"):
            with self.subTest(path=path.name): ast.parse(path.read_text(encoding="utf-8"),filename=str(path))


if __name__ == "__main__":
    unittest.main()
