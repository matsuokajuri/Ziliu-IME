"""Strict model payload and train-only target schema. No data discovery or IO."""
import hashlib
import json
import math

from .config import Config

TEACHERS = {"local:Qwen3.6-27B", "local:Gemma4-31B"}


def exact(value, fields):
    if type(value) is not dict or set(value) != set(fields):
        raise ValueError("unexpected or missing contract fields")


def digest(value):
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                     allow_nan=False).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def hash_value(value):
    if (type(value) is not str or len(value) != 64
            or any(c not in "0123456789abcdef" for c in value)):
        raise ValueError("explicit lowercase SHA256 required")


def text(value, limit, *, empty=False):
    if type(value) is not str or "\x00" in value or not (0 if empty else 1) <= len(value) <= limit:
        raise ValueError("bounded NUL-free Unicode text required")
    value.encode("utf-8", errors="strict")  # reject lone surrogates


def validate_request(request, config=Config()):
    exact(request, {"prefix", "pinyin", "candidates"})
    text(request["prefix"], config.max_prefix_scalars, empty=True)
    text(request["pinyin"], config.max_pinyin_scalars)
    if (any(c not in "abcdefghijklmnopqrstuvwxyz' " for c in request["pinyin"])
            or not any("a" <= c <= "z" for c in request["pinyin"])):
        raise ValueError("literal lowercase ASCII pinyin required")
    cs = request["candidates"]
    if type(cs) is not list or not 1 <= len(cs) <= config.max_candidates:
        raise ValueError("bounded candidate pool required")
    ids = []
    for c in cs:
        exact(c, {"source_index", "text"})
        ident = c["source_index"]
        if type(ident) is not int or not 0 <= ident < config.max_candidates or ident in ids:
            raise ValueError("unique original source identities within explicit candidate cap required")
        ids.append(ident)
        text(c["text"], config.max_candidate_scalars)
    if 0 not in ids:
        raise ValueError("native leader must remain in every compared pool")
    return ids


def finite_vector(values, count):
    if (type(values) is not list or len(values) != count
            or any(type(x) not in (int, float) or not math.isfinite(x) for x in values)):
        raise ValueError("complete finite vector required")


def validate_training_row(row, config=Config()):
    exact(row, {"schema", "split", "heldout", "purpose", "request", "pool_sha256",
                "document_id", "family_id", "admission_sha256", "target"})
    if (row["schema"] != "ziliu.direct-ranking-train.v1" or row["split"] != "train"
            or row["heldout"] is not False or row["purpose"] != "optimizer"):
        raise ValueError("only explicitly admitted train optimizer rows are allowed")
    for key in ("document_id", "family_id"):
        text(row[key], 128)
    hash_value(row["admission_sha256"])
    hash_value(row["pool_sha256"])
    ids = validate_request(row["request"], config)
    if row["pool_sha256"] != digest(row["request"]):
        raise ValueError("target and ordered exact candidate pool differ")
    target = row["target"]
    exact(target, {"kind", "status", "candidate_source_indices", "probabilities",
                   "acceptable_source_indices", "teachers", "response_provenance_sha256"})
    if target["candidate_source_indices"] != ids or any(type(i) is not int for i in target["candidate_source_indices"]):
        raise ValueError("target source identity/order differs")
    if target["kind"] not in ("teacher_distribution", "acceptable_set"):
        raise ValueError("known target kind required")
    if target["status"] not in ("resolved", "no_answer", "tie", "abstain", "disagreement", "refusal"):
        raise ValueError("explicit target status required")
    hash_value(target["response_provenance_sha256"])
    teachers = target["teachers"]
    if target["kind"] == "teacher_distribution":
        if type(teachers) is not list or len(teachers) != 2:
            raise ValueError("exactly two authorized local teacher manifests required")
        seen = set()
        for teacher in teachers:
            exact(teacher, {"id", "revision", "license_receipt_sha256"})
            if teacher["id"] not in TEACHERS or teacher["id"] in seen:
                raise ValueError("unauthorized or repeated teacher")
            seen.add(teacher["id"])
            text(teacher["revision"], 128)
            hash_value(teacher["license_receipt_sha256"])
        if target["acceptable_source_indices"] != []:
            raise ValueError("teacher preference cannot masquerade as semantic acceptance")
    elif teachers != [] or target["probabilities"] != []:
        raise ValueError("semantic acceptability and teacher distributions are separate")
    if target["status"] != "resolved":
        if target["probabilities"] != [] or target["acceptable_source_indices"] != []:
            raise ValueError("unresolved/zero-answer rows cannot invent rank targets")
        return None
    if target["kind"] == "teacher_distribution":
        p = target["probabilities"]
        finite_vector(p, len(ids))
        if any(v < 0 for v in p) or abs(sum(p)-1) > 1e-6:
            raise ValueError("normalized nonnegative teacher distribution required")
    else:
        acceptable = target["acceptable_source_indices"]
        if (type(acceptable) is not list or not acceptable
                or any(type(i) is not int or i not in ids for i in acceptable)
                or len(set(acceptable)) != len(acceptable)):
            raise ValueError("nonempty unique in-pool acceptable set required")
        membership_by_text = {}
        for candidate in row["request"]["candidates"]:
            accepted = candidate["source_index"] in acceptable
            previous = membership_by_text.setdefault(candidate["text"], accepted)
            if previous != accepted:
                # IDs remain distinct, but this scorer cannot distinguish identical
                # raw text. Reject contradictory semantic supervision; never relabel.
                raise ValueError("identical candidate text has conflicting acceptability")
    return target


def adapt_legacy_rank_row(legacy, *, document_id, family_id, admission_sha256, teachers):
    """Explicit migration of an already admitted old train row, never qualification-only.

    Does not read a file, manufacture admission, call a teacher or relax provenance.
    Fresh collection must freeze the added fields and licenses independently.
    """
    exact(legacy, {"split", "heldout", "admission_status", "request", "rank_target",
                   "response_provenance_sha256", "native_pool_sha256"})
    if legacy["admission_status"] != "FOUR_RESPONSE_NATIVE_GOLD_ADMITTED":
        raise ValueError("evaluation-only teacher qualifications are not training authorization")
    hash_value(legacy["native_pool_sha256"])
    ids = validate_request(legacy["request"])
    row = dict(schema="ziliu.direct-ranking-train.v1", split=legacy["split"],
               heldout=legacy["heldout"], purpose="optimizer", request=legacy["request"],
               pool_sha256=digest(legacy["request"]), document_id=document_id, family_id=family_id,
               admission_sha256=admission_sha256,
               target=dict(kind="teacher_distribution", status="resolved", candidate_source_indices=ids,
                           probabilities=legacy["rank_target"], acceptable_source_indices=[],
                           teachers=teachers, response_provenance_sha256=legacy["response_provenance_sha256"]))
    validate_training_row(row)
    # Keep the richer native-pool evidence outside the three-field model payload.
    return row, {"legacy_native_pool_sha256": legacy["native_pool_sha256"]}


def validate_split_manifest(manifest):
    """Check opaque keeper-provided split metadata, never discover/open text or labels.

    This detects declared collisions; it cannot discover undocumented near duplicates.
    """
    exact(manifest, {"train", "development", "confirmation"})
    owners = {"document_id": {}, "family_id": {}}
    for split, rows in manifest.items():
        if type(rows) is not list or len(rows) > 100_000:
            raise ValueError("bounded split metadata required")
        seen = set()
        for item in rows:
            exact(item, {"document_id", "family_id"})
            for field in owners:
                text(item[field],128)
                previous = owners[field].setdefault(item[field],split)
                if previous != split:
                    raise ValueError("document/family crosses frozen split boundary")
            pair = (item["document_id"],item["family_id"])
            if pair in seen:
                raise ValueError("duplicate family/document metadata row")
            seen.add(pair)
    return True
