"""Train/dev-only research policy separating source evidence from protection.

The frozen v1 policy and production contracts are unchanged. This interface
rejects production use. Ordinary exact system dictionary hits may be compared
only with whole-key/literal-source proof and agreeing conditional/context-gain
evidence. Explicit choices, dedicated fixed phrases and unknowns remain guarded.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

from offline_candidate_evidence import literal_alias_matches
from offline_rerank_protocol import validate_text


@dataclass(frozen=True)
class SourceEvidence:
    origin: str
    answers_key: bool | None
    literal_match: bool | None
    strong_protection: bool

    def __post_init__(self):
        origins = {"ordinary_system_exact", "system_composed", "explicit_history", "dedicated_fixed",
                   "user_learning_preference_unresolved", "table_preference_unresolved", "unknown"}
        if self.origin not in origins or type(self.strong_protection) is not bool:
            raise ValueError("known source class and explicit protection fact required")
        for value in (self.answers_key, self.literal_match):
            if value is not None and type(value) is not bool:
                raise ValueError("source facts must be bool or unknown")


@dataclass(frozen=True)
class Thresholds:
    conditional_margin: float
    contextual_gain_margin: float
    best_second_gap: float

    def __post_init__(self):
        for value in (self.conditional_margin, self.contextual_gain_margin, self.best_second_gap):
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise ValueError("nonnegative finite research thresholds required")


@dataclass(frozen=True)
class Decision:
    indices: tuple[int, ...]
    promotion: int | None
    reason: str


def source_evidence(candidate, keys, *, isolated_schema_qualified: bool,
                    explicit_choice_receipt: bool = False,
                    dedicated_phrase_receipt: bool = False):
    """Receipts may only add protection, never authenticate production use.

    The namespace qualification is the audited isolated Rime Ice schema. Native
    candidate type alone cannot certify arbitrary production translator sources.
    An ordinary user_phrase doesn't prove an explicit manual choice or pin.
    """
    for flag in (isolated_schema_qualified, explicit_choice_receipt, dedicated_phrase_receipt):
        if type(flag) is not bool:
            raise ValueError("explicit evidence qualification flags required")
    whole = candidate.get("answers_key")
    if whole is not None and type(whole) is not bool:
        raise ValueError("whole-key fact must be bool or unknown")
    if explicit_choice_receipt or dedicated_phrase_receipt:
        return SourceEvidence("explicit_history" if explicit_choice_receipt else "dedicated_fixed",
                              whole, None, True)
    if not isolated_schema_qualified:
        return SourceEvidence("unknown", whole, None, False)
    genuines = candidate.get("native_genuines", [])
    if len(genuines) != 1 or genuines[0].get("text") != candidate["text"]:
        return SourceEvidence("unknown", whole, None, False)
    genuine = genuines[0]
    literal = literal_alias_matches(keys, genuine["canonical_tokens"])
    dynamic, kind = genuine["dynamic_type"], genuine["type"]
    if dynamic == "Phrase" and kind == "phrase" and candidate.get("trusted_dictionary_hit") is True and literal:
        return SourceEvidence("ordinary_system_exact", whole, True, False)
    if dynamic == "Sentence" and kind == "sentence" and candidate.get("trusted_dictionary_hit") is False and literal:
        return SourceEvidence("system_composed", whole, True, False)
    if dynamic == "Phrase" and kind == "user_phrase":
        return SourceEvidence("user_learning_preference_unresolved", whole, literal, False)
    if dynamic == "Phrase" and kind == "user_table":
        return SourceEvidence("table_preference_unresolved", whole, None, False)
    return SourceEvidence("unknown", whole, None, False)


def research_gate(prefix, texts, evidence, *, ordinary_scope: bool = True,
                  current_prefix: bool = True, production_requested: bool = False):
    if any(type(flag) is not bool for flag in (ordinary_scope, current_prefix, production_requested)):
        raise ValueError("explicit scope evidence required")
    if production_requested:
        return Decision((), None, "production_not_admitted")
    if type(texts) not in (list, tuple) or type(evidence) not in (list, tuple):
        raise ValueError("explicit candidate and evidence arrays required")
    if type(prefix) is not str or len(prefix) > 16_384:
        raise ValueError("bounded research prefix required")
    if len(texts) != len(evidence) or len(texts) > 9:
        raise ValueError("research input bounds differ")
    if any(type(fact) is not SourceEvidence for fact in evidence):
        raise ValueError("explicit source evidence objects required")
    validate_text(prefix)
    for text in texts:
        validate_text(text)
    if not ordinary_scope:
        return Decision((), None, "scope_forbidden_or_unknown")
    if not current_prefix:
        return Decision((), None, "stale_prefix")
    if not prefix:
        return Decision((), None, "missing_field_prefix")
    if not evidence:
        return Decision((), None, "empty_pool")
    if any(fact.strong_protection for fact in evidence):
        return Decision((), None, "explicit_choice_or_dedicated_phrase")
    origins = {"ordinary_system_exact", "system_composed"}
    if evidence[0].origin not in origins:
        return Decision((), None, "leader_preference_or_source_unresolved")
    if evidence[0].answers_key is not True or evidence[0].literal_match is not True:
        return Decision((), None, "leader_spelling_or_consumption_unproven")
    indices = tuple(i for i, fact in enumerate(evidence) if fact.answers_key is True and
        fact.literal_match is True and fact.origin in origins)
    if len(indices) < 2:
        return Decision((), None, "fewer_than_two_verified_candidates")
    if any(not 1 <= len(texts[i]) <= 63 for i in indices):
        return Decision((), None, "complete_text_outside_model_bound")
    return Decision(indices, None, "eligible_offline_research")


def decide(gate, conditional, gain, thresholds):
    if not gate.indices:
        if conditional or gain:
            raise ValueError("scores for declined policy")
        return gate
    if (type(gate.indices) is not tuple or gate.indices[0] != 0 or
        any(type(i) is not int or not 0 <= i < 9 for i in gate.indices) or
        tuple(sorted(set(gate.indices))) != gate.indices):
        raise ValueError("policy lost immutable leader/source identity")
    if len(conditional) != len(gate.indices) or len(gain) != len(gate.indices):
        raise ValueError("score/source mapping differs")
    if any(type(value) not in (int, float) or not math.isfinite(value) for value in (*conditional, *gain)):
        raise ValueError("finite model scores required")
    best = max(range(len(conditional)), key=lambda i: (conditional[i], -i))
    contextual_best = max(range(len(gain)), key=lambda i: (gain[i], -i))
    if best == 0:
        return Decision(gate.indices, None, "native_leader_preferred")
    if best != contextual_best:
        return Decision(gate.indices, None, "conditional_context_gain_disagree")
    gap = conditional[best] - max(value for i, value in enumerate(conditional) if i != best)
    if (conditional[best] - conditional[0] <= thresholds.conditional_margin or
        gain[best] - gain[0] <= thresholds.contextual_gain_margin or
        gap <= thresholds.best_second_gap):
        return Decision(gate.indices, None, "insufficient_agreeing_context_evidence")
    return Decision(gate.indices, gate.indices[best], "promote_verified_ordinary_candidate")
