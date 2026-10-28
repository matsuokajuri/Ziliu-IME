# NumPy student research primitives and synthetic tests

Original Ziliu research source. **This is not a production input method or a qualified complete student model.** No weights, vocabulary, real candidate/input records, teacher labels, private runtime bindings or private recovery checkpoints are included.

## What is included

- Float64 reset-after-affine GRU primitives and independent scalar/finite-difference fixtures.
- Out-of-place AdamW with global gradient clipping, weight-only decay and scalar reference checks.
- A fixed **45-parameter H2/D3, four-example synthetic learner**, including step64 checkpoint and 64-update fresh-object replay in the **same process**. Checkpoint encoding includes parameters, both moments, optimizer configuration, RNG, cursor and caller-supplied experiment identity.
- `model.py`: a 288,769-trainable-parameter context-initialized dual-GRU proposal using caller-provided frozen embedding vectors; `objective_reference.py` provides the proposed pool objective. A separately pinned Linux experiment has now qualified a reduced **H2/D3 complete-model instance**: all 103 trainable coordinates were checked by finite differences, with a maximum absolute error of 4.65354e-11. See [the reviewed small-model aggregate](../CONTEXT-SMALL-PUBLIC-AGGREGATE.json). This does **not** qualify the H128 model, peak memory/time, training quality or Windows behavior; long singleton cases exercised geometry rather than nonzero-loss long-sequence gradients.

All 17 copied implementation/fixture/test files are byte-identical to their reviewed research sources. `SOURCE-MANIFEST.json` records their hashes without private source locations. New packaging documentation does not alter the frozen research experiment.

## Dependencies and stdlib checks

Python 3.12 is the tested source environment. Static tests require only the standard library and do not import NumPy, Torch or ctypes:

```sh
python -B -m unittest -v test_gru_static test_objective_reference test_adamw_toy_static_v2 test_independent_gru_oracle test_model_static
```

43 tests passed for this public candidate. Scalar arithmetic and finite differences in these tests are **not** complete-model NumPy qualification.

Actual injected-array experiments additionally require NumPy supplied by a separately admitted runtime. No dependency installer, downloadable model, private loader or automatic numerical entry point is bundled. Historical isolated primitive/toy results do not qualify an arbitrary installed NumPy/backend, this public package, or a new process automatically.

## Reproducing the synthetic learner under a controlled caller

The exact API is `gru_toy_learning.run(np, output_directory, observe, experiment_identity_sha256)`. Use a **new, empty, private output directory**. `observe(kind, event)` receives ordered `issued`/`completed` events; durable pre-operation journaling belongs in the caller. The supplied experiment identity must be a 64-character lowercase SHA256 digest that binds the exact sources, runtime, configuration and output namespace, not a convenient placeholder.

The function performs 128 primary updates, saves completed step64, restores into fresh objects, and replays updates65–128. It checks exact final state/RNG/cursor and predictions against the uninterrupted path. Fixed gates are final MSE <=0.01 and <=0.1 times initial MSE. **Do not change thresholds or silently retry a failed attempt.** Its budget is 198 requests, 192 updates, 1560 cell forwards and 1536 cell backwards. `adamw_synthetic_checks_v2.run(np, observe)` separately exercises 3 scalar-oracle updates plus 2 expected alias rejections (5 requests). Combined budget:203 requests,197 optimizer attempts/195 completed steps.

The original research resource contract is CPU2, owned address-space <=4GiB, fresh system available memory >=8GiB, a120-second owned-process window, one heavy job at a time, and fresh source/runtime/operation checks. A caller must enforce those limits before NumPy import and numerical work. No public numerical wrapper has been executed or qualified in this package. Do not assume environment variables alone prove actual BLAS thread count or executable-memory behavior.

`toy_checkpoint.restore` can reconstruct a separately provided synthetic checkpoint, but this package includes **no checkpoint bytes**. The demonstrated replay is fresh-object/same-process, **not fresh-process numerical recovery or remote-backup durability**. A new process/runtime requires its own pinned and reviewed restore qualification.

## Complete-model boundary

The complete context model accepts unpadded embedding arrays, not vocabulary/token metadata. An upstream adapter must enforce BOS/OOV/vocabulary/padding and source-split provenance. The model cannot recognize semantic padding from embedding values. Only TRAIN targets may enter the training adapter; DEV and final acceptance sets must not be used to tune it. Its returned cache is mutable; caller-array copies do not protect against deliberate cache mutation.

Only the reduced H2/D3 numerical derivatives described above have been qualified in a pinned research runtime. No full-width H128 derivatives or peak resource bound, real-data quality, final acceptance, release readiness, installation or Windows product acceptance is claimed. Model/teacher/embedding redistribution rights are not supplied by this code license.
