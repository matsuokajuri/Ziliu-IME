"""Synthetic standard-library tests for the public evaluation core only."""
from copy import deepcopy
import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import offline_evaluation_contracts as K
import offline_pool_evaluation as P


def candidate(index, text, verified=True):
    return {"source_index": index, "text": text, "answers_key": verified,
            "eligible": verified, "commit": text if verified else None,
            "remaining_input": "" if verified else "x",
            "remaining_preedit": "" if verified else "x",
            "source": {"origin": "system_composed" if verified else "unknown",
                       "answers_key": verified, "literal_match": verified,
                       "strong_protection": False}}


def freeze(value):
    menu = value["candidates"][:9]
    gate = P.research_gate(value["prefix"], [c["text"] for c in menu],
                          [P.SourceEvidence(**c["source"]) for c in menu],
                          **{k: value[k] for k in
                             ("ordinary_scope", "current_prefix", "production_requested")})
    value["preprocessing"] = K.context_plan(value["prefix"], [menu[i] for i in gate.indices])
    return value


def case(ident="synthetic-case"):
    return freeze({"id": ident, "input": "abcd", "prefix": "synthetic left context",
                   "ordinary_scope": True, "current_prefix": True,
                   "production_requested": False, "context_quality": "ordinary",
                   "candidates": [candidate(0, "alpha"), candidate(1, "hole", False),
                                  candidate(2, "gap", False), candidate(3, "delta")]})


class ToyScorer:
    def __init__(self):
        self.requests = []

    def score(self, request):
        self.requests.append(deepcopy(request))
        return {"candidate_source_indices": [c["source_index"] for c in request["candidates"]],
                "conditional": [0., 2.], "bos": [0., 0.]}


def bindings():
    return {key: K.digest({"synthetic_binding": key}) for key in K.BINDINGS}


def development_rows():
    return [{"model_id": "baseline_4m", "split": "development", "admitted": True,
             "context_quality": "ordinary", "conditional": [0., gap],
             "safe_to_promote": safe, "native_acceptable": False,
             "promotion_acceptable": safe, "net_improvement": safe,
             "family_token": family, "document_id": document}
            for gap, safe, family, document in
            ((1., True, "synthetic-family-a", "synthetic-document-a"),
             (2., True, "synthetic-family-b", "synthetic-document-b"),
             (.25, False, "synthetic-family-c", "synthetic-document-c"))]


def lock_and_receipt():
    pins = bindings()
    lock = P.calibrate_conditional(development_rows(), pins)
    receipt = {"schema": "ziliu.conditional-calibration-receipt.v2", "lock": lock,
               "decision_digest": K.digest(lock),
               "prediction_seal": {"model_id": "baseline_4m", "status": "COMPLETE",
                                   "labels_read": False,
                                   "input_sha256": pins["development_input_sha256"],
                                   "prediction_digest": pins["development_prediction_digest"],
                                   "source_pins_sha256": pins["source_pins_sha256"]}}
    return lock, receipt, pins


def verify(lock, receipt, pins):
    return K.verify_lock(lock, receipt, role="baseline_4m",
                         expected_bindings=pins, receipt_sha=K.digest(receipt))


def bundles(cases):
    predictions = [P.score_case(c, ToyScorer()) for c in cases]
    return {role: (deepcopy(predictions),
                   {"model_id": role, "input_sha256": K.digest("synthetic-input"),
                    "protocol_sha256": K.digest("synthetic-protocol"),
                    "labels_read": False, "status": "COMPLETE", "rows": len(cases),
                    "prediction_digest": P.digest(predictions)})
            for role in P.MODEL_ROLES}


class EvaluationCoreTests(unittest.TestCase):
    def test_imports_do_not_load_model_runtimes_or_unpublished_tools(self):
        for name in ("torch", "numpy", "onnxruntime", "offline_model_adapters",
                     "offline_small_lm", "offline_policy_loss", "offline_owned_job"):
            self.assertNotIn(name, sys.modules)

    def test_reference_aliases_and_unknown_extensions_stop_before_callback(self):
        for key in sorted(P.LABEL_KEYS | {"unknown_metadata"}):
            for location in ("case", "candidate", "source", "preprocessing"):
                with self.subTest(key=key, location=location):
                    c = case()
                    dest = {"case": c, "candidate": c["candidates"][0],
                            "source": c["candidates"][0]["source"],
                            "preprocessing": c["preprocessing"]}[location]
                    dest[key] = {"synthetic_only": True}
                    scorer = ToyScorer()
                    with self.assertRaises(ValueError):
                        P.score_case(c, scorer)
                    self.assertEqual(scorer.requests, [])

    def test_missing_or_truthy_context_admission_makes_zero_calls(self):
        for key in ("ordinary_scope", "current_prefix", "production_requested"):
            for value in (None, "false", 1, "missing"):
                with self.subTest(key=key, value=value):
                    c = case()
                    if value == "missing":
                        del c[key]
                    else:
                        c[key] = value
                    scorer = ToyScorer()
                    with self.assertRaises(ValueError):
                        P.score_case(c, scorer)
                    self.assertEqual(scorer.requests, [])

    def test_common_context_keeps_43_scalar_suffix_and_native_holes(self):
        c = case()
        c["prefix"] = "0123456789" * 4 + "abcdefgh"
        c["candidates"][0] = candidate(0, "a" * 20)
        freeze(c)
        scorer = ToyScorer()
        result = P.score_case(c, scorer)
        self.assertEqual(c["preprocessing"]["budget"], 43)
        self.assertEqual(scorer.requests[0]["prefix"], c["prefix"][-43:])
        self.assertEqual(result["candidate_source_indices"], [0, 3])
        self.assertEqual(set(scorer.requests[0]), {"prefix", "pinyin", "candidates"})
        c["preprocessing"]["retained_prefix"] = c["prefix"]
        with self.assertRaises(ValueError):
            P.score_case(c, ToyScorer())

    def test_empty_effective_context_declines_without_scoring(self):
        c = case()
        c["candidates"][0] = candidate(0, "a" * 63)
        freeze(c)
        scorer = ToyScorer()
        result = P.score_case(c, scorer)
        self.assertEqual(result["status"], "GATE_DECLINED")
        self.assertEqual(scorer.requests, [])

    def test_raw_conditional_lock_is_rejected_before_scoring(self):
        scorer = ToyScorer()
        with self.assertRaises(ValueError):
            P.score_case(case(), scorer, conditional_lock={"enabled": "false", "threshold": 0})
        self.assertEqual(scorer.requests, [])

    def test_verified_lock_requires_typed_complete_and_current_evidence(self):
        lock, receipt, pins = lock_and_receipt()
        accepted = verify(lock, receipt, pins)
        self.assertEqual(P.score_case(case(), ToyScorer(), conditional_lock=accepted)
                         ["conditional_promotion"], 3)
        for key, value in (("enabled", "false"), ("surviving_families", 0),
                           ("development_digest", "not-a-digest")):
            bad = deepcopy(lock)
            bad[key] = value
            with self.assertRaises(ValueError):
                verify(bad, receipt, pins)
        bad_pins = pins | {"model_digest": K.digest("different-model")}
        with self.assertRaises(ValueError):
            verify(lock, receipt, bad_pins)
        bad_receipt = deepcopy(receipt)
        bad_receipt["prediction_seal"]["labels_read"] = True
        with self.assertRaises(ValueError):
            verify(lock, bad_receipt, pins)

    def test_safe_alternatives_are_not_net_improvements(self):
        rows = development_rows()
        for row in rows[:2]:
            row.update(native_acceptable=True, net_improvement=False)
        lock = P.calibrate_conditional(rows, bindings())
        self.assertFalse(lock["enabled"])
        self.assertEqual(lock["safe_surviving_rows"], 2)
        self.assertEqual(lock["improvement_surviving_rows"], 0)

    def test_absent_unsafe_boundary_or_empty_evidence_disables(self):
        for rows in (development_rows()[:2], []):
            lock = P.calibrate_conditional(rows, bindings(), model_id="baseline_4m")
            self.assertFalse(lock["enabled"])
            self.assertEqual(lock["disabled_reason"], "no_unsafe_development_boundary")

    def test_admission_receipt_binds_explicit_context_facts(self):
        c = case()
        input_sha = K.digest("synthetic-input")
        receipt = {"schema": "ziliu.offline-admission.v2", "input_sha256": input_sha,
                   "collection_receipt_sha256": K.digest("synthetic-collection"),
                   "privacy_receipt_sha256": K.digest("synthetic-privacy"),
                   "cases": [{k: c[k] for k in ("id", "ordinary_scope", "current_prefix",
                                                "production_requested", "context_quality")}
                             | {"prefix_sha256": hashlib.sha256(c["prefix"].encode()).hexdigest()}]}
        K.validate_admission(receipt, [c], input_sha)
        for mutate in (lambda r: r.update(privacy_receipt_sha256=""),
                       lambda r: r["cases"][0].update(current_prefix=False)):
            bad = deepcopy(receipt)
            mutate(bad)
            with self.assertRaises(ValueError):
                K.validate_admission(bad, [c], input_sha)

    def test_preregistration_matches_split_model_and_preprocessing(self):
        protocol = {"candidate_cap": 9, "pool_timeout_ms": 500,
                    "strict_thresholds": {"conditional_margin": 1,
                                          "contextual_gain_margin": 1, "best_second_gap": .25},
                    "conditional_rule": K.RULE, "no_unsafe_sample_action": "disable",
                    "preprocessing": K.PREPROCESSING}
        models = {role: K.digest({"synthetic_model": role}) for role in P.MODEL_ROLES}
        phase = {"input_sha256": K.digest("synthetic-input"),
                 "admission_sha256": K.digest("synthetic-admission")}
        plan = {"schema": K.PLAN_SCHEMA, "rule": K.RULE,
                "preprocessing_sha256": K.digest(K.PREPROCESSING),
                "strategy_sha256": K.strategy_digest(protocol),
                "source_pins_sha256": K.digest("synthetic-source"), "backend_digests": models,
                "development": phase | {"annotations_sha256": K.digest("synthetic-annotations")},
                "confirmation": deepcopy(phase), "consumption_directory": "synthetic-consumed"}
        args = dict(split="confirmation", input_sha=phase["input_sha256"],
                    admission_sha=phase["admission_sha256"], protocol=protocol,
                    model_digests=models, source_digest=plan["source_pins_sha256"])
        K.validate_plan(plan, **args)
        bad = deepcopy(plan)
        bad["preprocessing_sha256"] = K.digest("different-preprocessing")
        with self.assertRaises(ValueError):
            K.validate_plan(bad, **args)

    def test_pairing_preserves_all_rows_and_reference_denominators(self):
        cases = [case()]
        result = P.pair_results(cases, bundles(cases),
                                [{"id": cases[0]["id"], "strict_reference_targets": ["delta"]}],
                                K.digest("synthetic-input"), K.digest("synthetic-protocol"))
        self.assertEqual(result["rows"], 1)
        self.assertFalse(result["production_enabled"])
        self.assertTrue(result["reference_labels_are_not_complete_semantic_answer_sets"])
        for model in result["models"].values():
            self.assertEqual(model["metrics"]["strict"]["corrections"], 1)
            self.assertEqual(model["metrics"]["locked_conditional"]["unchanged"], 1)

    def test_pairing_rejects_partial_digest_mismatch_and_label_reordering(self):
        cases = [case("synthetic-a"), case("synthetic-b")]
        labels = [{"id": c["id"], "strict_reference_targets": ["delta"]} for c in cases]
        for mutate in (lambda b: b["cassotis"][1].update(status="PARTIAL_TIMEOUT"),
                       lambda b: b["trained_4m"][1].update(prediction_digest=K.digest("changed"))):
            bad = bundles(cases)
            mutate(bad)
            with self.assertRaises(ValueError):
                P.pair_results(cases, bad, labels, K.digest("synthetic-input"), K.digest("synthetic-protocol"))
        with self.assertRaises(ValueError):
            P.pair_results(cases, bundles(cases), labels[::-1],
                           K.digest("synthetic-input"), K.digest("synthetic-protocol"))

    def test_original_comparator_separates_recovery_from_product_gain(self):
        cases = [case()]
        originals = deepcopy(cases)
        originals[0]["candidates"][0] = candidate(0, "delta")
        freeze(originals[0])
        result = P.pair_results(cases, bundles(cases),
                                [{"id": cases[0]["id"], "strict_reference_targets": ["delta"]}],
                                K.digest("synthetic-input"), K.digest("synthetic-protocol"),
                                original_cases=originals)
        for model in result["models"].values():
            metrics = model["metrics"]["strict"]
            self.assertEqual(metrics["corrections"], 1)
            self.assertEqual(metrics["product_net_vs_original"], 0)


if __name__ == "__main__":
    unittest.main()
