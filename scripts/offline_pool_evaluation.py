"""Model-free contracts, pairing and development-only conditional calibration."""
from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import time

from offline_context_policy_v2 import Decision, SourceEvidence, Thresholds, research_gate, decide
from offline_rerank_protocol import scoring_request, validate_text
from offline_evaluation_contracts import (BINDINGS, LOCK_SCHEMA, RULE, VerifiedConditionalLock,
    context_plan, exact, hash_value)

MODEL_ROLES = ("baseline_4m", "trained_4m", "cassotis")
LABEL_KEYS = {"expected", "gold", "target", "label", "should_abstain", "strict_reference_targets", "acceptable_targets",
              "source_continuation", "right_context", "reference_text", "source_reference", "strict_source_reference",
              "acceptable_texts", "pre_native_possible_acceptable_texts", "acceptable_set_closed"}
STRICT_THRESHOLDS = Thresholds(1, 1, .25)
CASE_FIELDS = {"id","input","prefix","candidates","ordinary_scope","current_prefix","production_requested",
               "context_quality","preprocessing"}
CANDIDATE_FIELDS = {"source_index","text","answers_key","eligible","commit","remaining_input","remaining_preedit","source"}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def file_sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def save_json(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


def no_labels(value):
    if isinstance(value, dict):
        if LABEL_KEYS.intersection(value):
            raise ValueError("label-bearing inference input")
        for v in value.values():
            no_labels(v)
    elif isinstance(value, list):
        for v in value:
            no_labels(v)


def validate_cases(cases):
    no_labels(cases)
    if type(cases) is not list or not 1 <= len(cases) <= 10_000:
        raise ValueError("bounded explicit case list required")
    seen = set()
    for case in cases:
        exact(case,CASE_FIELDS,"only explicit inference case fields are permitted")
        if any(type(case[k]) is not bool for k in ("ordinary_scope","current_prefix","production_requested")):
            raise ValueError("explicit bool context/scope/production admission required")
        if case["context_quality"] not in ("ordinary","weak","unknown"):
            raise ValueError("explicit structural context quality required")
        ident = case.get("id")
        if type(ident) is not str or not ident or ident in seen:
            raise ValueError("unique case identity required")
        seen.add(ident)
        cs = case["candidates"]
        if not 1 <= len(cs) <= 200:
            raise ValueError("native menu must contain 1..200 entries")
        for i, c in enumerate(cs):
            exact(c,CANDIDATE_FIELDS,"only native inference candidate fields are permitted")
            exact(c["source"],{"origin","answers_key","literal_match","strong_protection"},"only qualified source facts are permitted")
            validate_text(c["text"])
            if type(c["source_index"]) is not int or c["source_index"] != i:
                raise ValueError("native menu identity/order lost")
            fact = SourceEvidence(**c["source"])
            if c["answers_key"] is not None and type(c["answers_key"]) is not bool:
                raise ValueError("consumption fact must be bool or unknown")
            if type(c.get("eligible")) is not bool:
                raise ValueError("explicit eligibility fact required")
            if fact.answers_key != c["answers_key"]:
                raise ValueError("conflicting consumption evidence")
            if c.get("eligible"):
                if (c["answers_key"] is not True or c["commit"] != c["text"] or
                        c["remaining_input"] or c["remaining_preedit"]):
                    raise ValueError("eligible candidate lacks complete real commit")
        gate,_ = gate_and_request(case)
        if any(cs[i]["eligible"] is not True for i in gate.indices):
            raise ValueError("scoring gate includes unverified commit")
    return cases


def gate_and_request(case):
    cs = case["candidates"][:9]
    gate = research_gate(case["prefix"], [c["text"] for c in cs], [SourceEvidence(**c["source"]) for c in cs],
        ordinary_scope=case["ordinary_scope"], current_prefix=case["current_prefix"],
        production_requested=case["production_requested"])
    plan=context_plan(case["prefix"],[cs[i] for i in gate.indices])
    if canonical(case["preprocessing"])!=canonical(plan):
        raise ValueError("frozen common context budget/retained suffix differs")
    # Validate literal input even for a declined row, without sending a request.
    scoring_request({"prefix":"","input":case["input"]},[{"source_index":0,"text":"x"}])
    if gate.indices and not plan["retained_prefix"]:
        gate=Decision((),None,"empty_retained_context")
    request = scoring_request({"prefix": plan["retained_prefix"], "input": case["input"]}, [cs[i] for i in gate.indices]) if gate.indices else None
    return gate, request


def validate_scores(value, indices):
    if set(value) not in ({"candidate_source_indices", "conditional", "bos"},
                          {"candidate_source_indices", "conditional", "bos", "gain"}):
        raise ValueError("unexpected scorer output fields")
    if value["candidate_source_indices"] != list(indices):
        raise ValueError("scorer lost original source identity/order")
    if type(value["candidate_source_indices"]) is not list or any(type(i) is not int for i in value["candidate_source_indices"]):
        raise ValueError("source identity must be explicit integer indices")
    C, B = value["conditional"], value["bos"]
    if any(type(v) is not list or len(v) != len(indices) for v in (C, B)):
        raise ValueError("score count differs")
    if any(type(x) not in (int, float) or not math.isfinite(x) for x in C+B):
        raise ValueError("finite scores required")
    G = [c-b for c,b in zip(C,B)]
    if "gain" in value and value["gain"] != G:
        raise ValueError("gain definition changed")
    return C, B, G


def conditional_choice(case, gate, C, lock):
    """One gap parameter; every source/privacy/fixed gate remains upstream."""
    if lock is not None and not isinstance(lock,VerifiedConditionalLock):
        raise ValueError("conditional policy requires a verified pinned development receipt")
    if not gate.indices or lock is None or not lock.enabled:
        return None, "conditional_policy_disabled_or_declined"
    tau = lock.threshold
    if not case["preprocessing"]["retained_prefix"] or case["context_quality"] != "ordinary":
        return None, "weak_empty_or_unknown_context"
    best = max(range(len(C)), key=lambda i:(C[i],-i))
    if best == 0:
        return None, "native_leader_preferred"
    gap = C[best]-max(x for i,x in enumerate(C) if i != best)
    return (gate.indices[best], "conditional_gap_pass") if gap > tau else (None, "conditional_gap_insufficient")


def score_case(case, scorer, *, timeout_ms=500, conditional_lock=None, clock=time.monotonic):
    if conditional_lock is not None and not isinstance(conditional_lock,VerifiedConditionalLock):
        raise ValueError("unverified conditional lock; no scorer call permitted")
    validate_cases([case])
    gate, request = gate_and_request(case)
    out = {"id":case["id"], "candidate_source_indices":list(gate.indices), "gate_reason":gate.reason,
           "conditional":[], "bos":[], "gain":[], "strict_promotion":None, "raw_conditional_source_index":0,
           "conditional_promotion":None, "conditional_reason":"not_scored", "status":"GATE_DECLINED", "elapsed_ms":0.0,
           "preprocessing":case["preprocessing"]}
    if request is None:
        return out
    if type(timeout_ms) is not int or not 1 <= timeout_ms <= 2_000:
        raise ValueError("per-pool timeout must be 1..2000 ms")
    start = clock()
    try:
        value = scorer.score(request)
    except TimeoutError:
        out.update(status="TIMEOUT", elapsed_ms=max(0,(clock()-start)*1000))
        return out
    elapsed = (clock()-start)*1000
    if not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError("invalid elapsed time")
    if elapsed > timeout_ms:
        out.update(status="TIMEOUT",elapsed_ms=elapsed)
        return out
    C,B,G = validate_scores(value, gate.indices)
    strict = decide(gate,C,G,STRICT_THRESHOLDS)
    promotion, reason = conditional_choice(case,gate,C,conditional_lock)
    raw = gate.indices[max(range(len(C)),key=lambda i:(C[i],-i))]
    out.update(conditional=C,bos=B,gain=G,strict_promotion=strict.promotion,strict_reason=strict.reason,
               raw_conditional_source_index=raw,conditional_promotion=promotion,conditional_reason=reason,
               status="SCORED",elapsed_ms=elapsed)
    return out


def calibrate_conditional(rows, bindings, *, model_id=None):
    """One boundary: max unsafe development gap, then lock; no grid search."""
    if (not rows and model_id not in MODEL_ROLES) or any(r.get("split") != "development" for r in rows):
        raise ValueError("development rows only; confirmation cannot select a threshold")
    roles = {r.get("model_id") for r in rows} if rows else {model_id}
    if model_id is not None and roles!={model_id}:raise ValueError("calibration role differs")
    if len(roles)!=1 or not roles.issubset(MODEL_ROLES):
        raise ValueError("one explicit model role per calibration; never mix score scales")
    exact(bindings,BINDINGS,"complete frozen development evidence bindings required")
    if not all(hash_value(v) for v in bindings.values()):raise ValueError("invalid evidence digest")
    usable = []
    for r in rows:
        if any(type(r.get(k)) is not bool for k in ("safe_to_promote","native_acceptable","promotion_acceptable","net_improvement")):
            raise ValueError("separate safety and native/promotion/net-improvement facts required")
        expected_improvement=r["safe_to_promote"] and not r["native_acceptable"] and r["promotion_acceptable"]
        if r["net_improvement"]!=expected_improvement or (r["safe_to_promote"] and not r["promotion_acceptable"]):
            raise ValueError("harmless alternative cannot be counted as a net improvement")
        if type(r.get("admitted")) is not bool or any(type(r.get(k)) is not str or not r[k] for k in ("family_token","document_id")):
            raise ValueError("explicit admission and independently audited family/document identities required")
        C = r["conditional"]
        if type(C) is not list or len(C)>9 or any(type(x) not in (int,float) or not math.isfinite(x) for x in C):
            raise ValueError("finite complete pool required")
        if not r.get("admitted") or r.get("context_quality") != "ordinary":
            continue
        if len(C)<2:raise ValueError("admitted pool requires at least two scores")
        best = max(range(len(C)),key=lambda i:(C[i],-i))
        if best:
            usable.append((r,C[best]-max(x for i,x in enumerate(C) if i != best)))
    tau = max([0.0]+[g for r,g in usable if not r["safe_to_promote"]])
    safe_survivors=[r for r,g in usable if g>tau and r["safe_to_promote"]]
    survivors = [r for r in safe_survivors if r["net_improvement"]]
    families, docs = {r["family_token"] for r in survivors}, {r["document_id"] for r in survivors}
    unsafe=sum(not r["safe_to_promote"] for r,_ in usable)
    enabled = unsafe>0 and len(families)>=2 and len(docs)>=2
    return {"schema":LOCK_SCHEMA, "model_id":next(iter(roles)), "calibration_split":"development", "threshold":tau,
            "enabled":enabled,"rule":RULE, "development_digest":digest(rows),"bindings":dict(bindings),
            "safe_surviving_rows":len(safe_survivors),"improvement_surviving_rows":len(survivors),
            "surviving_families":len(families),"surviving_documents":len(docs),"unsafe_proposals":unsafe,
            "disabled_reason":None if enabled else ("no_unsafe_development_boundary" if not unsafe else
                                                     "insufficient_cross_family_document_improvement")}


def verify_prediction_bundle(cases, bundles, input_sha, protocol_sha, conditional_locks=None):
    validate_cases(cases)
    if set(bundles) != set(MODEL_ROLES):
        raise ValueError("exactly the three frozen model roles required")
    ids = [c["id"] for c in cases]
    for role, (predictions,seal) in bundles.items():
        if (seal["model_id"] != role or seal["input_sha256"] != input_sha or seal["protocol_sha256"] != protocol_sha or
            seal["labels_read"] is not False or seal["status"] != "COMPLETE" or seal["rows"] != len(ids) or
            seal["prediction_digest"] != digest(predictions) or [p["id"] for p in predictions] != ids):
            raise ValueError("unsealed, partial, reordered or incompatible predictions")
        for case,p in zip(cases,predictions):
            gate,_ = gate_and_request(case)
            lock=(conditional_locks or {}).get(role)
            if lock is not None and (not isinstance(lock,VerifiedConditionalLock) or lock.model_id!=role):
                raise ValueError("verified lock model role differs")
            if list(gate.indices) != p["candidate_source_indices"] or p["preprocessing"]!=case["preprocessing"]:
                raise ValueError("prediction gate differs")
            for key in ("raw_conditional_source_index","strict_promotion","conditional_promotion"):
                if p[key] is not None and type(p[key]) is not int:
                    raise ValueError("decision source index cannot be bool or another type")
            if p["status"] == "SCORED":
                C,B,G = validate_scores({k:p[k] for k in ("candidate_source_indices","conditional","bos","gain")},gate.indices)
                d = decide(gate,C,G,STRICT_THRESHOLDS)
                if p["strict_promotion"] != d.promotion or p["raw_conditional_source_index"] != gate.indices[max(range(len(C)),key=lambda i:(C[i],-i))]:
                    raise ValueError("score/decision mismatch")
                expected,_ = conditional_choice(case,gate,C,lock)
                if p["conditional_promotion"] != expected:
                    raise ValueError("conditional decision differs from locked development policy")
            elif (p["status"] != "GATE_DECLINED" or gate.indices or p["conditional"] or p["bos"] or p["gain"] or
                  p["strict_promotion"] is not None or p["conditional_promotion"] is not None or p["raw_conditional_source_index"] != 0):
                raise ValueError("invalid declined or timed-out seal")


def pair_results(cases, bundles, labels, input_sha, protocol_sha, conditional_locks=None, original_cases=None):
    verify_prediction_bundle(cases,bundles,input_sha,protocol_sha,conditional_locks)
    ids = [c["id"] for c in cases]
    if [l["id"] for l in labels] != ids:
        raise ValueError("labels have different identity/order; no implicit inner join")
    if original_cases is not None:
        validate_cases(original_cases)
        if [c["id"] for c in original_cases]!=ids or any((a["input"],a["prefix"])!=(b["input"],b["prefix"]) for a,b in zip(original_cases,cases)):
            raise ValueError("original/new pool case identity, input or context differs")
    for label in labels:
        refs = label["strict_reference_targets"]
        if type(refs) is not list or (not refs and label.get("must_preserve_native") is not True) or any(type(t) is not str or not t or "\x00" in t for t in refs):
            raise ValueError("explicit complete reference text list required")
        for t in refs:validate_text(t)
        if "must_preserve_native" in label and type(label["must_preserve_native"]) is not bool:
            raise ValueError("explicit control annotation required")
    result = {}
    for role,(predictions,_) in bundles.items():
        outcomes = []
        for position,(case,p,label) in enumerate(zip(cases,predictions,labels)):
            ref = label["strict_reference_targets"]
            good = lambda c: c.get("eligible") is True and c["answers_key"] is True and c["commit"]==c["text"] and not c["remaining_input"] and not c["remaining_preedit"] and c["text"] in ref
            choices = {"native":0,"raw_conditional":p["raw_conditional_source_index"],"strict":p["strict_promotion"] if p["strict_promotion"] is not None else 0,
                       "locked_conditional":p["conditional_promotion"] if p["conditional_promotion"] is not None else 0}
            for method,i in choices.items():
                if i and i not in p["candidate_source_indices"]:
                    raise ValueError("promotion outside verified native IDs")
                c = case["candidates"][i]
                outcomes.append({"id":case["id"],"method":method,"source_index":i,"text":c["text"],
                                 "reference_match":bool(good(c)),"native_reference_match":bool(good(case["candidates"][0])),
                                 "original_reference_match":bool(good(original_cases[position]["candidates"][0])) if original_cases is not None else None,
                                 "reference_target_present":bool(ref),"must_preserve_native":label.get("must_preserve_native",False),
                                 "changed":i!=0,"decision_reason":p["gate_reason"] if p["status"]!="SCORED" else
                                 {"native":"native_order","raw_conditional":"conditional_argmax",
                                  "strict":p.get("strict_reason"),"locked_conditional":p.get("conditional_reason")}[method]})
        metrics = {}
        for method in ("native","raw_conditional","strict","locked_conditional"):
            rows = [o for o in outcomes if o["method"]==method]
            metrics[method] = {"rows":len(rows),"reference_top1":sum(o["reference_match"] for o in rows),
                "reference_target_rows":sum(o["reference_target_present"] for o in rows),
                "preservation_control_rows":sum(o["must_preserve_native"] for o in rows),
                "preservation_control_interventions":sum(o["must_preserve_native"] and o["changed"] for o in rows),
                "corrections":sum(o["reference_match"] and not o["native_reference_match"] for o in rows),
                "reference_losses":sum(o["native_reference_match"] and not o["reference_match"] for o in rows),
                "nonreference_to_other_nonreference":sum(o["changed"] and not o["reference_match"] and not o["native_reference_match"] for o in rows),
                "unchanged":sum(not o["changed"] for o in rows)}
            if original_cases is not None:
                metrics[method].update(original_reference_top1=sum(o["original_reference_match"] for o in rows),
                    product_net_vs_original=sum(o["reference_match"]-o["original_reference_match"] for o in rows),
                    original_reference_losses=sum(o["original_reference_match"] and not o["reference_match"] for o in rows))
        result[role] = {"metrics":metrics,"outcomes":outcomes,"status_counts":dict(Counter(p["status"] for p in predictions))}
    return {"models":result,"rows":len(cases),"reference_labels_are_not_complete_semantic_answer_sets":True,
            "candidate_recall":sum(any(c.get("eligible") is True and c.get("commit")==c["text"] and not c.get("remaining_input") and not c.get("remaining_preedit") and c["text"] in l["strict_reference_targets"] for c in case["candidates"]) for case,l in zip(cases,labels)),
            "production_enabled":False}
