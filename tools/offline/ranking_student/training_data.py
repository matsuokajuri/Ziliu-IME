"""Bounded, pinned train-only bridge. No discovery, teacher calls or heavy imports."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import random

from .codec import Codec
from .contracts import (adapt_legacy_rank_row, digest, exact, hash_value, text,
                        validate_request, validate_split_manifest)

EXCLUDED_COLLECTIONS = {"legacy_59_templates", "consumed_development_330", "exploration_96"}
MAX_ROWS = 512
MAX_BYTES = 8 * 1024 * 1024
ARTIFACTS = {"native_cases", "native_admission", "labels", "lineage"}
SEED = 20261007
STEPS = 64


def select_training_cohort(rows):
    """The existing fixed-seed, one-pass selection, shared by prepare and worker."""
    order = list(range(len(rows)))
    random.Random(SEED).shuffle(order)
    chosen, documents, families = [], set(), set()
    while order and len(chosen) < STEPS:
        index = max(order, key=lambda i: (
            rows[i]["document_id"] not in documents)
            + (rows[i]["family_id"] not in families))
        order.remove(index); chosen.append(index)
        documents.add(rows[index]["document_id"])
        families.add(rows[index]["family_id"])
    if len(chosen) != STEPS or min(len(documents), len(families)) < 8:
        raise ValueError("scheduled subset does not preserve minimum group coverage")
    return chosen


def input_order_binding(rows, identities, order):
    """Bind ordered row IDs and complete requests without model runtime or IO."""
    if (type(rows) is not list or type(identities) is not list or len(rows) != len(identities)
            or type(order) is not list or not 1 <= len(order) <= STEPS
            or any(type(i) is not int or not 0 <= i < len(rows) for i in order)
            or len(set(order)) != len(order)):
        raise ValueError("bounded unique input order required")
    result = []
    for index in order:
        row, ident = rows[index], identities[index]
        text(ident, 128)
        ids = validate_request(row["request"])
        pool_sha = digest(row["request"])
        if row["pool_sha256"] != pool_sha:
            raise ValueError("input order request and frozen pool digest differ")
        result.append(dict(row_id=ident, pool_sha256=pool_sha, candidate_source_indices=ids))
    if len({item["row_id"] for item in result}) != len(result):
        raise ValueError("input order row identities must be unique")
    return result


def validate_input_order_binding(binding, rows, identities, order):
    expected = input_order_binding(rows, identities, order)
    if type(binding) is not list or len(binding) != len(expected):
        raise ValueError("complete ordered input binding required")
    for item, wanted in zip(binding, expected):
        exact(item, {"row_id", "pool_sha256", "candidate_source_indices"})
        text(item["row_id"], 128)
        hash_value(item["pool_sha256"])
        ids = item["candidate_source_indices"]
        if type(ids) is not list or any(type(i) is not int for i in ids) or item != wanted:
            raise ValueError("input binding identity, request digest or candidate order differs")
    return expected


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def parse_json(raw):
    def reject(_):
        raise ValueError("nonfinite JSON constant")
    return json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=reject)


def read_pinned(path, expected, limit=MAX_BYTES):
    hash_value(expected)
    path = Path(path)
    if path.suffix.lower() != ".json":
        raise ValueError("explicit JSON artifact required; never load weights")
    with path.open("rb") as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit or hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError("artifact size or byte SHA256 differs")
    return parse_json(raw)


def index_by_id(rows):
    if type(rows) is not list or not 1 <= len(rows) <= MAX_ROWS:
        raise ValueError("one to 512 explicit train rows required")
    result = {}
    for row in rows:
        if type(row) is not dict:
            raise ValueError("object row required")
        text(row.get("id"), 128)
        if row["id"] in result:
            raise ValueError("duplicate row identity")
        result[row["id"]] = row
    return result


def validate_lineage(lineage):
    exact(lineage, {"schema", "purpose", "origin", "authorization_receipt_sha256",
                    "split_audit_sha256", "excluded_collections", "exclusions",
                    "split_manifest", "records"})
    if (lineage["schema"] != "ziliu.ranking-training-lineage.v1"
            or lineage["purpose"] != "optimizer_train_only"
            or lineage["origin"] not in {"real_rime_train", "synthetic_fixture"}):
        raise ValueError("explicit train-only lineage required")
    for key in ("authorization_receipt_sha256", "split_audit_sha256"):
        hash_value(lineage[key])
    excluded = lineage["excluded_collections"]
    if (type(excluded) is not list or any(type(x) is not str for x in excluded)
            or len(excluded) != len(set(excluded)) or not EXCLUDED_COLLECTIONS <= set(excluded)):
        raise ValueError("59/330/96 collection exclusions must remain explicit")
    exact(lineage["exclusions"], {"document_ids", "family_ids", "request_sha256"})
    for field, values in lineage["exclusions"].items():
        if type(values) is not list or len(values) > 100_000:
            raise ValueError("bounded opaque exclusion metadata required")
        for value in values:
            hash_value(value) if field == "request_sha256" else text(value, 128)
    validate_split_manifest(lineage["split_manifest"])
    records = index_by_id(lineage["records"])
    registered = {(r["document_id"], r["family_id"])
                  for r in lineage["split_manifest"]["train"]}
    observed = set()
    for record in records.values():
        exact(record, {"id", "document_id", "family_id", "collection_id", "label_row_sha256",
                       "native_case_sha256", "native_pool_sha256", "admission_sha256", "teachers"})
        for key in ("document_id", "family_id", "collection_id"):
            text(record[key], 128)
        for key in ("label_row_sha256", "native_case_sha256", "native_pool_sha256", "admission_sha256"):
            hash_value(record[key])
        if (record["collection_id"] in excluded
                or record["document_id"] in lineage["exclusions"]["document_ids"]
                or record["family_id"] in lineage["exclusions"]["family_ids"]):
            raise ValueError("consumed/held-out lineage cannot become first-round training")
        observed.add((record["document_id"], record["family_id"]))
    if observed != registered:
        raise ValueError("train records and frozen split membership differ")
    return records


def bridge_legacy_bundle(cases, admission, labels, lineage, native_input_sha256):
    """Compare real native inference contract to the old rank_rows interface.

    Hashes bind reviewed declarations; they do not authenticate their issuer or
    prove document independence. No labels/probabilities are invented or repaired.
    """
    from offline_evaluation_contracts import validate_admission
    from offline_pool_evaluation import gate_and_request, validate_cases
    records = validate_lineage(lineage)
    native = index_by_id(cases)
    validate_cases(cases)
    validate_admission(admission, cases, native_input_sha256)
    exact(labels, {"rank_rows", "language_anchors"})
    # Existing bundles may include train language anchors; they are not consumed.
    if type(labels["language_anchors"]) is not list:
        raise ValueError("legacy language_anchors must be a list")
    old_rows = index_by_id(labels["rank_rows"])
    if set(records) != set(native) or set(records) != set(old_rows):
        raise ValueError("label/native/lineage IDs differ; no fuzzy join or silent drop")
    rows, identities, seen_requests = [], [], set()
    for ident, old in old_rows.items():
        record, case = records[ident], native[ident]
        if digest(old) != record["label_row_sha256"] or digest(case) != record["native_case_sha256"]:
            raise ValueError("native/label row differs from reviewed lineage")
        if old.get("native_pool_sha256") != record["native_pool_sha256"]:
            raise ValueError("legacy native capture receipt differs")
        _, request = gate_and_request(case)
        if request is None or case["context_quality"] != "ordinary":
            raise ValueError("privacy/source/userfixed/context gate declined training row")
        if request != old.get("request"):
            raise ValueError("exact retained context/pinyin/ordered candidate IDs/text differ")
        request_sha = digest(request)
        if request_sha in seen_requests or request_sha in lineage["exclusions"]["request_sha256"]:
            raise ValueError("duplicate or consumed exact training request")
        seen_requests.add(request_sha)
        # Only the observed legacy id extension is removed. Unknown fields fail.
        legacy = {key: value for key, value in old.items() if key != "id"}
        migrated, _ = adapt_legacy_rank_row(legacy, document_id=record["document_id"],
            family_id=record["family_id"], admission_sha256=record["admission_sha256"],
            teachers=record["teachers"])
        # The old four-response contract emits one-hot choices, not confidence.
        p = migrated["target"]["probabilities"]
        if sum(v == 1 for v in p) != 1 or any(v not in (0, 1) for v in p):
            raise ValueError("legacy hard-choice admission requires one-hot rank_target")
        by_text = {}
        for candidate, probability in zip(request["candidates"], p):
            if by_text.setdefault(candidate["text"], probability) != probability:
                raise ValueError("identical candidate text cannot carry conflicting hard choices")
        rows.append(migrated)
        identities.append(ident)
    return rows, identities


def build_preparation(cases, admission, labels, lineage, native_input_sha256):
    rows, identities = bridge_legacy_bundle(cases, admission, labels, lineage, native_input_sha256)
    # Freeze only from admitted train input text, never labels or dev/confirmation.
    counts = Counter(char for r in rows for value in [r["request"]["prefix"], r["request"]["pinyin"],
        *[c["text"] for c in r["request"]["candidates"]]] for char in value)
    characters = sorted(counts, key=lambda char: (-counts[char], ord(char)))[:8192-260]
    codec = Codec(characters)
    shapes = []
    for row in rows:
        plan = codec.plan([row["request"]])
        shapes.append((len(plan["source_ids"][0]), len(plan["candidate_ids"][0]),
                       len(plan["candidate_ids"][0][0])))
    documents = len({r["document_id"] for r in rows})
    families = len({r["family_id"] for r in rows})
    blockers = []
    if lineage["origin"] != "real_rime_train": blockers.append("SYNTHETIC_FIXTURE_ONLY")
    if len(rows) < 64: blockers.append("NEED_64_UNIQUE_ADMITTED_REQUESTS")
    if min(documents, families) < 8: blockers.append("NEED_8_DOCUMENTS_AND_8_FAMILIES")
    if any(s > 128 or t > 128 for s, _, t in shapes): blockers.append("FIRST_RUN_TOKEN_CAP_EXCEEDED")
    training_order, cohort = None, None
    if not blockers:
        try:
            training_order = select_training_cohort(rows)
        except ValueError:
            blockers.append("SCHEDULED_COHORT_GROUP_COVERAGE")
        else:
            cohort = input_order_binding(rows, identities, training_order)
    summary = dict(status="READY_FOR_AUTHORIZED_WINDOW" if not blockers else "DATA_BLOCKED",
        blockers=blockers, rows=len(rows), unique_documents=documents, unique_families=families,
        origin=lineage["origin"], max_source_tokens=max(s for s, _, _ in shapes),
        max_candidates=max(k for _, k, _ in shapes), max_candidate_tokens=max(t for _, _, t in shapes),
        codec_sha256=codec.sha256, ignored_language_anchors=len(labels["language_anchors"]),
        teachers=sorted({t["id"] for r in rows for t in r["target"]["teachers"]}),
        probabilities_are="one_hot_hard_choices_not_teacher_confidence",
        training_cohort=cohort, training_cohort_sha256=digest(cohort) if cohort is not None else None,
        runtime_loaded=False, optimizer_steps=0, quality_claim=False, production_enabled=False)
    return dict(rows=rows, ids=identities, characters=characters, codec=codec, summary=summary,
                training_order=training_order)


def prepare(manifest_path, manifest_sha256):
    manifest_path = Path(manifest_path).resolve()
    manifest = read_pinned(manifest_path, manifest_sha256, 64*1024)
    exact(manifest, {"schema", "purpose", "artifacts"})
    if manifest["schema"] != "ziliu.ranking-training-input.v1" or manifest["purpose"] != "optimizer_train_only":
        raise ValueError("explicit train-only input manifest required")
    exact(manifest["artifacts"], ARTIFACTS)
    objects = {}
    # Inspect train-only authority/membership before opening the label artifact.
    for name in ("lineage", "native_admission", "native_cases", "labels"):
        ref = manifest["artifacts"][name]
        exact(ref, {"path", "sha256"})
        text(ref["path"], 4096)
        objects[name] = read_pinned(manifest_path.parent / ref["path"], ref["sha256"])
        if name == "lineage": validate_lineage(objects[name])
    result = build_preparation(objects["native_cases"], objects["native_admission"], objects["labels"],
                               objects["lineage"], manifest["artifacts"]["native_cases"]["sha256"])
    result["summary"].update(input_manifest_sha256=manifest_sha256,
                            artifact_pins={k: v["sha256"] for k, v in manifest["artifacts"].items()})
    return result
