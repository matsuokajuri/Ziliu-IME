"""Model-free admission and metrics for an isolated Rime reranking experiment.

No production policy, user database, network, candidate generation or model load.
Engine facts are evidence supplied by the collector, never inferred from gold.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Iterable, Sequence

MODEL_CHAR_LIMIT = 64
COMPARED = 9


@dataclass(frozen=True)
class CandidateFacts:
    answers_key: bool | None
    trusted_dictionary_hit: bool | None


@dataclass(frozen=True)
class Gate:
    indices: tuple[int, ...]
    reason: str


def validate_text(text: str) -> None:
    if not isinstance(text, str) or "\x00" in text:
        raise ValueError("text must be NUL-free Unicode")
    text.encode("utf-8", errors="strict")


def scoring_request(row: dict, candidates: Sequence[dict]) -> dict:
    """Allowlist the exact inference payload; research rows are never serialized.

    Labels belong in separate evaluator files. Grouping metadata may contain
    answer atoms and must remain outside teacher/student requests, even when it
    accompanies an otherwise valid inference input.
    """
    if any(key in row for key in ("expected", "gold", "target", "label", "should_abstain")):
        raise ValueError("label-bearing row cannot enter model request")
    prefix = row["prefix"] if "prefix" in row else row["context"]
    if "prefix" in row and "context" in row and row["prefix"] != row["context"]:
        raise ValueError("conflicting context fields")
    validate_text(prefix)
    if len(prefix) > 16_384:
        raise ValueError("prefix exceeds inference bound")
    keys = row["input"]
    if type(keys) is not str or not 1 <= len(keys) <= 64 or not keys.isascii():
        raise ValueError("bounded literal ASCII pinyin required")
    if not keys.isalpha() or keys != keys.lower():
        raise ValueError("literal lowercase pinyin required")
    if not 1 <= len(candidates) <= COMPARED:
        raise ValueError("model comparison pool must contain 1..9 candidates")
    values, seen = [], set()
    for candidate in candidates:
        index, text = candidate["source_index"], candidate["text"]
        if type(index) is not int or not 0 <= index < 200 or index in seen:
            raise ValueError("invalid or duplicate immutable candidate source index")
        seen.add(index)
        validate_text(text)
        if not 1 <= len(text) <= 63:
            raise ValueError("complete candidate exceeds inference bound")
        values.append({"source_index": index, "text": text})
    return {"prefix": prefix, "pinyin": keys, "candidates": values}


def reference_gate(facts: Sequence[CandidateFacts]) -> Gate:
    """best_where's gate, with explicit refusal when engine evidence is unknown.

The upstream API accepts booleans. This adapter must not manufacture them when
the existing Rime collector cannot establish a candidate's source.
"""
    if not facts:
        return Gate((), "empty_pool")
    leader = facts[0]
    if leader.answers_key is not True:
        return Gate((), "leader_whole_key_unknown_or_false")
    if leader.trusted_dictionary_hit is None:
        return Gate((), "leader_dictionary_provenance_unknown")
    if leader.trusted_dictionary_hit:
        return Gate((), "protected_dictionary_leader")
    # Unknown non-leaders are excluded instead of guessed to cover the key.
    indices = tuple(i for i, fact in enumerate(facts)
                    if fact.answers_key is True)[:COMPARED]
    if len(indices) < 2:
        return Gate((), "fewer_than_two_whole_key_candidates")
    return Gate(indices, "admitted")


def safe_field_gate(context: str, texts: Sequence[str],
                    facts: Sequence[CandidateFacts], *, protected: bool = False) -> Gate:
    """Extra Ziliu research guards, reported separately from the upstream gate."""
    if len(texts) != len(facts):
        raise ValueError("candidate/fact arrays differ")
    validate_text(context)
    for text in texts:
        validate_text(text)
    if protected:
        return Gate((), "protected_phrase_or_scope")
    if not context:
        return Gate((), "missing_field_prefix")
    gate = reference_gate(facts)
    if not gate.indices:
        return gate
    if any(not texts[i] or len(texts[i]) >= MODEL_CHAR_LIMIT for i in gate.indices):
        # The upstream scorer truncates overlong tails. Do not score a prefix
        # and then describe that as a decision about the complete candidate.
        return Gate((), "empty_or_overlong_complete_candidate")
    return gate


def choose(gate: Gate, scores: Sequence[float], pool_size: int) -> int | None:
    """Return a promotion index, or None to preserve the original leader."""
    if not gate.indices:
        if scores:
            raise ValueError("scores supplied for a declined decision")
        return None
    if len(scores) != len(gate.indices) or len(set(gate.indices)) != len(gate.indices):
        raise ValueError("score/identity mapping differs")
    if gate.indices[0] != 0 or any(not 0 <= i < pool_size for i in gate.indices):
        raise ValueError("gate lost the leader or a source index")
    if any(not math.isfinite(score) for score in scores):
        raise ValueError("nonfinite model score")
    # Strict improvement preserves the Rime leader on exact ties.
    best = max(range(len(scores)), key=lambda i: (scores[i], -i))
    return gate.indices[best] if best else None


def conditional_gain(conditional: Sequence[float],
                     unconditional: Sequence[float]) -> list[float]:
    """Mean logP(c|prefix) - mean logP(c|BOS); this is not a probability."""
    if len(conditional) != len(unconditional):
        raise ValueError("conditional/unconditional candidate arrays differ")
    if any(not math.isfinite(value) for value in (*conditional, *unconditional)):
        raise ValueError("nonfinite conditional gain input")
    return [left - right for left, right in zip(conditional, unconditional)]


def assert_family_isolation(rows: Iterable[dict]) -> dict:
    """Reject family, alias or ambiguity-atom leakage before collecting labels."""
    owner: dict[tuple[str, str], str] = {}
    ids = set()
    counts: dict[str, int] = {}
    for row in rows:
        if row["id"] in ids:
            raise ValueError("duplicate case ID")
        ids.add(row["id"])
        split = row["split"]
        if split not in {"train", "dev", "test"}:
            raise ValueError("unknown split")
        if not row.get("family") or not row.get("ambiguity_atoms"):
            raise ValueError("missing family/ambiguity grouping")
        # Canonical spellings and every actual typed alias must stay together.
        groups = [("family", row["family"]), ("input", row["input"])]
        groups += [("atom", atom) for atom in row["ambiguity_atoms"]]
        groups += [("input", key) for key in row.get("canonical_inputs", [])]
        for group in groups:
            previous = owner.setdefault(group, split)
            if previous != split:
                raise ValueError(f"cross-split overlap: {group[0]} {group[1]}")
        counts[split] = counts.get(split, 0) + 1
    return {"rows_by_split": counts, "groups": len(owner), "unique_ids": len(ids)}


def metrics(rows: Sequence[dict]) -> dict:
    """Frozen-denominator counts. Recall misses and abstentions remain visible.

    Each row contains the real candidate strings, a predeclared expected set
    (empty for explicit abstention), and an optional promotion source index.
    The expected set is consumed here, never by either gate or the model.
    """
    result = {"rows": len(rows), "targeted": 0, "explicit_abstention": 0,
              "empty_pool": 0, "rime_top1": 0, "method_top1": 0,
              "corrections": 0, "breakages": 0, "wrong_to_wrong_changes": 0,
              "abstention_violations": 0, "protected_violations": 0,
              "no_promotion": 0, "recall_counts": {str(k): 0 for k in (1, 9, 45, 200)}}
    for row in rows:
        texts = row["candidates"]
        expected = set(row["expected"])
        promotion = row.get("promotion")
        if promotion is not None and (type(promotion) is not int or not 0 < promotion < len(texts)):
            raise ValueError("invalid promotion index")
        changed = promotion is not None
        result["no_promotion"] += not changed
        result["protected_violations"] += bool(row.get("protected")) and changed
        if not expected:
            result["explicit_abstention"] += 1
            result["abstention_violations"] += changed
            continue
        result["targeted"] += 1
        if not texts:
            result["empty_pool"] += 1
            continue
        original = texts[0] in expected
        selected = texts[promotion if changed else 0] in expected
        result["rime_top1"] += original
        result["method_top1"] += selected
        result["corrections"] += not original and selected
        result["breakages"] += original and not selected
        result["wrong_to_wrong_changes"] += changed and not original and not selected
        for cutoff in (1, 9, 45, 200):
            result["recall_counts"][str(cutoff)] += any(t in expected for t in texts[:cutoff])
    return result
