"""Native offline observations; no inference from expected word or text length.

This module proves experiment facts only. It cannot admit production fields,
dictionary epochs, unqualified observers or unauthenticated evidence files.
"""
from __future__ import annotations

from offline_rerank_protocol import CandidateFacts, validate_text

MARKER = "\x1fZILIU_OFFLINE:"


def parse_observation(comment, *, nonce, keys, source_index, text):
    if (type(nonce) is not str or len(nonce) != 32 or any(c not in "0123456789abcdef" for c in nonce)
            or type(source_index) is not int or not 0 <= source_index < 200
            or type(keys) is not str or not 1 <= len(keys) <= 64 or not keys.isascii()):
        raise ValueError("invalid observer lifetime or literal input identity")
    validate_text(text)
    if type(comment) is not str or len(comment) > 65_536 or MARKER not in comment:
        raise ValueError("native observer evidence missing or exceeds bound")
    original, data = comment.rsplit(MARKER, 1)
    parts = data.split("|")
    if not 5 <= len(parts) <= 21 or parts[:3] != ["v1", nonce, str(source_index)]:
        raise ValueError("observer scope or immutable source index differs")
    def decode(value):
        if len(value) > 4096:
            raise ValueError("observer string exceeds bound")
        decoded = bytes.fromhex(value).decode("utf-8", errors="strict")
        validate_text(decoded)
        return decoded
    if decode(parts[3]) != keys or decode(parts[4]) != text:
        raise ValueError("observer input or candidate text differs")
    genuines = []
    for part in parts[5:]:
        values = part.split(",")
        if len(values) != 9:
            raise ValueError("native candidate geometry differs")
        dynamic, kind, start, end, remaining, canonical, spans, preedit, raw_text = values
        start, end = int(start), int(end)
        if not 0 <= start <= end <= 64:
            raise ValueError("native candidate span exceeds bound")
        remaining = None if remaining == "?" else int(remaining)
        if remaining is not None and not 0 <= remaining <= 64:
            raise ValueError("predictive remainder exceeds bound")
        vertices = [int(vertex) for vertex in spans.split(":")] if spans else []
        if (any(not 0 <= vertex <= 64 for vertex in vertices) or
                vertices != sorted(set(vertices))):
            raise ValueError("native syllable vertices differ")
        genuines.append({"dynamic_type": decode(dynamic), "type": decode(kind),
            "start": start, "end": end, "remaining_code_length": remaining,
            "canonical_tokens": decode(canonical).split(), "vertices": vertices,
            "preedit": decode(preedit), "text": decode(raw_text)})
    return {"nonce": nonce, "input": keys, "source_index": source_index,
            "text": text, "genuines": genuines, "original_comment": original}


def literal_alias_matches(keys, tokens):
    """Declared full/half/initial aliases of actual native code, never gold code."""
    if not tokens or any(not token.isascii() or not token.isalpha() or
                         token != token.lower() for token in tokens):
        return False
    return keys in {"".join(tokens), "".join(token[0] for token in tokens),
                   tokens[0] + "".join(token[0] for token in tokens[1:])}


def candidate_facts(observation, selection, *, qualified_observer):
    if type(qualified_observer) is not bool or not qualified_observer:
        return CandidateFacts(None, None)
    identity = ("nonce", "input", "source_index", "text")
    if any(observation.get(key) != selection.get(key) for key in identity):
        raise ValueError("selection and observation identity differs")
    whole_commit = (selection.get("selected") is True and
        selection.get("commit") == observation["text"] and
        selection.get("remaining_input") == "" and selection.get("remaining_preedit") == "")
    genuines = observation["genuines"]
    if not genuines:
        return CandidateFacts(None, None)
    if not whole_commit or any(g["start"] != 0 or g["end"] != len(observation["input"])
        or g["type"] == "completion" or (g["remaining_code_length"] is not None and
        g["remaining_code_length"] != 0) for g in genuines):
        return CandidateFacts(False, None)
    if any(g["remaining_code_length"] is None for g in genuines):
        return CandidateFacts(None, None)
    # Multiple underlying derivations or output transformations remain unknown.
    if len(genuines) != 1 or genuines[0]["text"] != observation["text"]:
        return CandidateFacts(True, None)
    g = genuines[0]
    if g["dynamic_type"] == "Sentence" and g["type"] == "sentence":
        return CandidateFacts(True, False)
    if (g["dynamic_type"] == "Phrase" and g["type"] in {"phrase", "user_phrase"}
            and literal_alias_matches(observation["input"], g["canonical_tokens"])):
        return CandidateFacts(True, True)
    # ScriptTranslator doesn't expose its IsCorrection path attribute in this
    # pinned API. Nonmatching code does not by itself prove an untrusted source.
    return CandidateFacts(True, None)
