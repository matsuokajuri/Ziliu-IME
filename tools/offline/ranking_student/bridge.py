"""Diagnostic scores only, through the existing immutable offline admission gate.

Caller exposes reviewed repository scripts/ on sys.path, as existing offline CLIs do.
No runtime is imported here; score_fn must be prepared only in an authorized Job.
"""
from .contracts import finite_vector, validate_request
from .reference import ranked_source_indices


def score_offline_case(case, score_fn):
    from offline_pool_evaluation import validate_cases, gate_and_request
    validate_cases([case])
    gate, request = gate_and_request(case)
    result = {"id":case["id"], "promotion":None, "production_enabled":False,
              "candidate_source_indices":[], "rank_logits":[], "diagnostic_order":[],
              "reason":gate.reason}
    if request is None:
        return result
    if case["context_quality"] != "ordinary":
        result["reason"] = "weak_or_unknown_context"
        return result
    ids = validate_request(request)
    logits = score_fn(request)
    finite_vector(logits,len(ids))
    result.update(candidate_source_indices=ids,rank_logits=logits,
                  diagnostic_order=ranked_source_indices(ids,logits),reason="diagnostic_only_policy_off")
    return result
