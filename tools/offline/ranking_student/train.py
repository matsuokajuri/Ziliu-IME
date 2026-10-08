"""Prepare by default; --run is an explicitly permitted owned-Job worker only."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

from .config import Config
from .contracts import digest, finite_vector
from .training_data import (SEED, STEPS, prepare, select_training_cohort,
                            input_order_binding, validate_input_order_binding)
from .training_window import authorize

LEARNING_RATE = 1e-3


def schedule(prepared, *, qualification=False):
    if qualification:
        if (prepared["summary"]["origin"] != "synthetic_fixture"
                or not 1 <= len(prepared["rows"]) <= 4
                or prepared["summary"]["max_source_tokens"] > 128
                or prepared["summary"]["max_candidate_tokens"] > 128):
            raise ValueError("pipeline qualification permits only one to four bounded synthetic rows")
        return list(range(len(prepared["rows"])))
    if prepared["summary"]["status"] != "READY_FOR_AUTHORIZED_WINDOW":
        raise ValueError("data readiness blockers; no optimizer execution")
    order = prepared.get("training_order")
    if order != select_training_cohort(prepared["rows"]):
        raise ValueError("worker order differs from the prepared fixed cohort")
    cohort = validate_input_order_binding(prepared["summary"].get("training_cohort"),
                                         prepared["rows"], prepared["ids"], order)
    if prepared["summary"].get("training_cohort_sha256") != digest(cohort):
        raise ValueError("prepared cohort receipt digest differs")
    return list(order)  # One pass only; consume the cohort verified before READY.


def save_json(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


def synthetic_prediction_order(prepared, binding):
    """Consume explicit input identities before any fixed prediction is made."""
    order = schedule(prepared, qualification=True)[:4]
    validate_input_order_binding(binding, prepared["rows"], prepared["ids"], order)
    return order


def validate_synthetic_prediction_receipt(report, prepared):
    """Stdlib binding/shape checks only; not authorization or numerical qualification."""
    if (type(report) is not dict or report.get("purpose") != "synthetic_pipeline_qualification"
            or report.get("status") != "SYNTHETIC_PIPELINE_QUALIFIED"):
        raise ValueError("completed synthetic prediction receipt required")
    order = synthetic_prediction_order(prepared, report.get("synthetic_input_order"))
    for key in ("fixed_predictions_before", "fixed_predictions_after", "fixed_predictions_restored"):
        predictions = report.get(key)
        if type(predictions) is not list or len(predictions) != len(order):
            raise ValueError("one fixed prediction vector per bound input required")
        for vector, index in zip(predictions, order):
            finite_vector(vector, len(prepared["rows"][index]["request"]["candidates"]))
    if (report.get("restored_fixed_predictions_exact") is not True
            or report["fixed_predictions_after"] != report["fixed_predictions_restored"]):
        raise ValueError("restored fixed prediction receipt differs")
    history = report.get("history")
    if (type(report.get("optimizer_steps")) is not int or report["optimizer_steps"] != len(order)
            or type(history) is not list or len(history) != len(order)
            or any(type(item) is not dict for item in history)
            or [item.get("row_id") for item in history] != [prepared["ids"][i] for i in order]):
        raise ValueError("synthetic step history differs from bound input order")
    return order


def optimize_once(model, optimizer, prepared, order, check_window, finite, report):
    """Bounded loop, separable from runtime import for synthetic control-flow tests."""
    codec = prepared["codec"]
    for index in order:
        check_window()
        row = prepared["rows"][index]
        plan = codec.plan([row["request"]])
        model.train(); optimizer.zero_grad(set_to_none=True)
        mark = time.monotonic()
        scores = model(**model.tensors(plan))
        loss = model.objective(scores, plan, [row])
        model.backward_checked(loss)
        if any(p.grad is None for p in model.parameters()):
            raise RuntimeError("missing parameter gradient; update refused")
        norm = model.finite_gradient_norm()
        check_window()  # Expiry during forward/backward must prevent the update.
        optimizer.step()
        report["optimizer_steps"] += 1
        if any(not finite(p) for p in model.parameters()):
            raise RuntimeError("nonfinite parameters after update; no checkpoint")
        report["history"].append(dict(step=report["optimizer_steps"],
            row_id=prepared["ids"][index], loss=float(loss.detach()),
            gradient_norm=float(norm), seconds=time.monotonic()-mark))


def run(prepared, output, check_window, permit):
    qualification = permit.get("purpose") == "synthetic_pipeline_qualification"
    order = schedule(prepared, qualification=qualification)
    check_window()
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    report = dict(status="PARTIAL", optimizer_steps=0, seed=SEED, learning_rate=LEARNING_RATE,
                  batch_size=1, maximum_steps=4 if qualification else STEPS, dataset=prepared["summary"], history=[],
                  checkpoint=None, production_enabled=False, quality_claim=False,
                  initialization="random", teacher_calls=0, training_rows_repeated=False,
                  purpose="synthetic_pipeline_qualification" if qualification else "optimizer_training")
    if qualification:
        report["synthetic_input_order"] = input_order_binding(prepared["rows"], prepared["ids"], order[:4])
    try:
        import torch
        import numpy as np
        from .model import build_model
        if torch.__version__ != permit["torch_version"] or np.__version__ != permit["numpy_version"]:
            raise RuntimeError("imported runtime differs from frozen installed versions")
        torch.set_num_threads(2); torch.set_num_interop_threads(1)
        torch.set_default_device("cpu"); torch.manual_seed(SEED)
        torch.use_deterministic_algorithms(True)
        if torch.cuda.is_initialized(): raise RuntimeError("CUDA initialized")
        check_window()
        model = build_model(Config(), runtime_authorized=True)
        codec = prepared["codec"]
        model.bind_codec(codec.sha256)
        optimizer = torch.optim.SGD(model.parameters(), lr=LEARNING_RATE, momentum=0, foreach=False)
        report["actual_parameters"] = sum(p.numel() for p in model.parameters())
        def predict(instance):
            instance.eval()
            with torch.inference_mode():
                return [instance(**instance.tensors(codec.plan([prepared["rows"][i]["request"]])))[0].tolist()
                        for i in synthetic_prediction_order(prepared, report["synthetic_input_order"])]
        if qualification:
            report["fixed_predictions_before"] = predict(model)
            initial_hashes = {n:hashlib.sha256(p.detach().cpu().numpy().tobytes()).hexdigest()
                              for n,p in model.named_parameters()}
        save_json(output/"TOKENIZER.json", dict(schema="char-utf8-fallback.v1",
            characters=prepared["characters"], vocab_size=8192, codec_sha256=codec.sha256))
        optimize_once(model, optimizer, prepared, order, check_window,
                      lambda p: bool(torch.isfinite(p).all()), report)
        check_window()
        model.eval()
        if qualification:
            report["fixed_predictions_after"] = predict(model)
            report["changed_parameter_tensors"] = sum(initial_hashes[n] !=
                hashlib.sha256(p.detach().cpu().numpy().tobytes()).hexdigest()
                for n,p in model.named_parameters())
            if not report["changed_parameter_tensors"]: raise RuntimeError("no parameter changed")
        # Save only the predetermined completed stage; no dev-selected checkpoint.
        checkpoint = output/"FINAL.npz"
        with checkpoint.open("xb") as stream:
            np.savez(stream, **{name:p.detach().cpu().numpy() for name,p in model.named_parameters()})
        with np.load(checkpoint, allow_pickle=False) as archive:
            expected = dict(model.named_parameters())
            if set(archive.files) != set(expected): raise RuntimeError("checkpoint tensor names differ")
            for name, parameter in expected.items():
                actual = archive[name]
                if (actual.dtype != np.float32 or actual.shape != tuple(parameter.shape)
                        or not np.array_equal(actual, parameter.detach().cpu().numpy())):
                    raise RuntimeError("checkpoint roundtrip differs")
        check_window()
        if qualification:
            restored = build_model(Config(), runtime_authorized=True)
            restored.bind_codec(codec.sha256)
            with np.load(checkpoint, allow_pickle=False) as archive, torch.no_grad():
                for name, parameter in restored.named_parameters():
                    parameter.copy_(torch.from_numpy(archive[name]))
            report["fixed_predictions_restored"] = predict(restored)
            if report["fixed_predictions_after"] != report["fixed_predictions_restored"]:
                raise RuntimeError("restored fixed predictions differ")
            report["restored_fixed_predictions_exact"] = True
            del restored
        check_window()
        if torch.cuda.is_initialized(): raise RuntimeError("CUDA initialized")
        report.update(status="SYNTHETIC_PIPELINE_QUALIFIED" if qualification else "BOUNDED_TRAINING_COMPLETE", checkpoint=dict(file=checkpoint.name,
            sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(), allow_pickle=False,
            safe_roundtrip=True, selection="synthetic_qualification_only" if qualification else "predetermined_64_unique_train_requests"))
        if qualification:
            validate_synthetic_prediction_receipt(report, prepared)
    except TimeoutError:
        report["stop_reason"] = "WINDOW_ENDED_NO_AUTOMATIC_EXTENSION"
    except BaseException as error:
        report.update(status="FAILED", error_type=type(error).__name__)
        raise
    finally:
        report["worker_seconds"] = time.monotonic()-started
        save_json(output/"TRAINING-RESULT.json", report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--permit", type=Path)
    parser.add_argument("--permit-sha256")
    args = parser.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]/"scripts"))
    if args.run:
        # Authorization is checked before opening any train artifact or importing torch.
        check, permit = authorize(args.permit, args.permit_sha256, args.manifest_sha256)
    elif args.permit or args.permit_sha256:
        parser.error("prepare mode does not accept a runtime permit")
    prepared = prepare(args.manifest, args.manifest_sha256)
    if args.run:
        report = run(prepared, args.out, check, permit)
        if report["status"] == "SYNTHETIC_PIPELINE_QUALIFIED":
            validate_synthetic_prediction_receipt(report, prepared)
        return 0 if report["status"] in {"BOUNDED_TRAINING_COMPLETE", "SYNTHETIC_PIPELINE_QUALIFIED"} else 2
    save_json(args.out, prepared["summary"])
    print(json.dumps(prepared["summary"], ensure_ascii=False))
    return 0 if prepared["summary"]["status"] == "READY_FOR_AUTHORIZED_WINDOW" else 2


if __name__ == "__main__":
    raise SystemExit(main())
