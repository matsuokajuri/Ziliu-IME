"""Standard-library-only preprocessing, admission and calibration receipts."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math


PREPROCESSING = {"schema":"ziliu.common-context.v1", "max_prefix_scalars":48,
                 "sequence_content_scalars":63, "rule":"left_suffix_min_48_63_minus_max_candidate"}
RULE = "single_max_unsafe_gap_boundary"
LOCK_SCHEMA = "ziliu.conditional-confidence-lock.v2"
PLAN_SCHEMA = "ziliu.offline-calibration-preregistration.v2"
BINDINGS = {"model_digest","preprocessing_sha256","strategy_sha256","plan_sha256","source_pins_sha256",
            "development_input_sha256","development_admission_sha256","development_prediction_sha256",
            "development_prediction_digest","development_seal_sha256","development_annotations_sha256"}


def canonical(value):
    return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(",",":"),allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def hash_value(value):
    return type(value) is str and len(value)==64 and all(c in "0123456789abcdef" for c in value)


def exact(value, fields, message):
    if type(value) is not dict or set(value)!=set(fields):
        raise ValueError(message)


def context_plan(prefix, candidates):
    if not candidates:
        budget, retained = 0, ""
    else:
        budget=min(48,63-max(len(c["text"]) for c in candidates))
        if budget<0:raise ValueError("candidate exceeds shared sequence budget")
        retained=prefix[-budget:] if budget else ""
    return {"schema":PREPROCESSING["schema"],"candidate_source_indices":[c["source_index"] for c in candidates],
            "budget":budget,"retained_prefix":retained,
            "retained_prefix_sha256":hashlib.sha256(retained.encode("utf-8")).hexdigest()}


def strategy_digest(protocol):
    return digest({k:protocol[k] for k in ("candidate_cap","pool_timeout_ms","strict_thresholds","conditional_rule",
                                         "no_unsafe_sample_action","preprocessing")})


def validate_admission(receipt, cases, input_sha):
    exact(receipt,{"schema","input_sha256","collection_receipt_sha256","privacy_receipt_sha256","cases"},
          "explicit label-free admission receipt required")
    if receipt["schema"]!="ziliu.offline-admission.v2" or receipt["input_sha256"]!=input_sha:
        raise ValueError("admission input binding differs")
    if not all(hash_value(receipt[k]) for k in ("collection_receipt_sha256","privacy_receipt_sha256")):
        raise ValueError("frozen collection/privacy receipt byte pins required")
    expected=[{k:c[k] for k in ("id","ordinary_scope","current_prefix","production_requested","context_quality")}
              | {"prefix_sha256":hashlib.sha256(c["prefix"].encode("utf-8")).hexdigest()} for c in cases]
    if canonical(receipt["cases"])!=canonical(expected):
        raise ValueError("context admission facts differ from frozen receipt")


def validate_plan(plan, *, split, input_sha, admission_sha, protocol, model_digests, source_digest):
    exact(plan,{"schema","rule","preprocessing_sha256","strategy_sha256","source_pins_sha256","backend_digests",
                "development","confirmation","consumption_directory"},"complete preregistration required")
    if (plan["schema"]!=PLAN_SCHEMA or plan["rule"]!=RULE or plan["preprocessing_sha256"]!=digest(PREPROCESSING)
            or plan["strategy_sha256"]!=strategy_digest(protocol) or plan["source_pins_sha256"]!=source_digest
            or plan["backend_digests"]!=model_digests or split not in ("development","confirmation")):
        raise ValueError("preregistered model/source/preprocessing/strategy bindings differ")
    for name in ("development","confirmation"):
        fields={"input_sha256","admission_sha256"} | ({"annotations_sha256"} if name=="development" else set())
        exact(plan[name],fields,"exact split pins required")
        if not all(hash_value(v) for v in plan[name].values()):raise ValueError("valid split digests required")
    if plan[split]["input_sha256"]!=input_sha or plan[split]["admission_sha256"]!=admission_sha:
        raise ValueError("input/admission not preregistered for this split")
    if type(plan["consumption_directory"]) is not str or not plan["consumption_directory"]:
        raise ValueError("one preregistered calibration receipt directory required")


@dataclass(frozen=True)
class VerifiedConditionalLock:
    model_id: str
    enabled: bool
    threshold: float
    receipt_sha256: str


def verify_lock(lock, receipt, *, role, expected_bindings, receipt_sha):
    """Only pinned, complete development evidence can create an executable lock."""
    fields={"schema","model_id","calibration_split","enabled","threshold","rule","development_digest","bindings",
            "safe_surviving_rows","improvement_surviving_rows","surviving_families","surviving_documents",
            "unsafe_proposals","disabled_reason"}
    exact(lock,fields,"complete calibrated lock required")
    if (lock["schema"]!=LOCK_SCHEMA or lock["model_id"]!=role or lock["calibration_split"]!="development"
            or lock["rule"]!=RULE or type(lock["enabled"]) is not bool or not hash_value(lock["development_digest"])
            or not hash_value(receipt_sha)):
        raise ValueError("typed development lock/provenance required")
    tau=lock["threshold"]
    if type(tau) not in (int,float) or not math.isfinite(tau) or tau<0:raise ValueError("finite threshold required")
    exact(lock["bindings"],BINDINGS,"complete evidence bindings required")
    if not all(hash_value(v) for v in lock["bindings"].values()):raise ValueError("valid binding digests required")
    if any(lock["bindings"].get(k)!=v for k,v in expected_bindings.items()):raise ValueError("current frozen bindings differ")
    counts=("safe_surviving_rows","improvement_surviving_rows","surviving_families","surviving_documents","unsafe_proposals")
    if any(type(lock[k]) is not int or lock[k]<0 for k in counts):raise ValueError("explicit nonnegative counts required")
    if not (lock["surviving_families"]<=lock["improvement_surviving_rows"]<=lock["safe_surviving_rows"] and
            lock["surviving_documents"]<=lock["improvement_surviving_rows"]):raise ValueError("inconsistent improvement counts")
    if lock["enabled"] and (min(lock["surviving_families"],lock["surviving_documents"])<2 or
                            lock["unsafe_proposals"]<1 or lock["disabled_reason"] is not None):
        raise ValueError("enabled lock lacks unsafe boundary and cross-document net improvements")
    if not lock["enabled"] and lock["disabled_reason"] not in (
            "no_unsafe_development_boundary","insufficient_cross_family_document_improvement"):
        raise ValueError("disabled lock reason required")
    exact(receipt,{"schema","lock","prediction_seal","decision_digest"},"pinned calibration receipt required")
    if (receipt["schema"]!="ziliu.conditional-calibration-receipt.v2" or receipt["lock"]!=lock
            or receipt["decision_digest"]!=digest(lock)):
        raise ValueError("calibration receipt/lock differs")
    seal=receipt["prediction_seal"]
    exact(seal,{"model_id","status","labels_read","input_sha256","prediction_digest","source_pins_sha256"},
          "label-free development seal snapshot required")
    if (seal["model_id"]!=role or seal["status"]!="COMPLETE" or seal["labels_read"] is not False or
            seal["input_sha256"]!=lock["bindings"]["development_input_sha256"] or
            seal["prediction_digest"]!=lock["bindings"]["development_prediction_digest"] or
            seal["source_pins_sha256"]!=lock["bindings"]["source_pins_sha256"]):
        raise ValueError("development seal does not support calibration lock")
    return VerifiedConditionalLock(role,lock["enabled"],float(tau),receipt_sha)
