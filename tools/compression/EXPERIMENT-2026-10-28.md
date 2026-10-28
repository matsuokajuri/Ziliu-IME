# Tiny sequence scorer: fixed 48-update feasibility result

This is a new reconstruction experiment, not an exact continuation of a lost
historical training run. It is a Linux CPU research result, not Windows or
input-method product acceptance.

## Fixed protocol

- Frozen teacher: R59 checkpoint; 128 scenes from its existing training pool.
- Student optimization: first 96 scenes, 234 conditions, 2,106 candidate scores.
- Student-only diagnostic: remaining 32 scenes, 85 conditions, 765 scores.
  The teacher previously saw these scenes. This is **not an independent teacher
  generalization test**. No final acceptance examples were opened.
- 48 predetermined AdamW updates, batch size 8 scenes, 4 passes in fixed order.
  No checkpoint selection or diagnostic-driven tuning.
- 23,041 trainable parameters plus 1,572,864 frozen embedding parameters:
  **1,595,905 total**, not a standalone 23K-parameter language model.
- Seven bounded CPU windows, each with a fresh memory check, two CPU threads,
  a 4 GiB process address-space ceiling and a 240-second alarm.

## Fixed final checkpoint measurements

| Measure | Student optimization split | Student-only diagnostic split |
|---|---:|---:|
| Teacher top-1 agreement | 22.65% | 17.65% |
| Teacher pairwise agreement | 66.35% | 61.37% |
| Centered score MAE | 0.253271 | 0.276909 |
| Centered score RMSE | 0.329819 | 0.375560 |

Scores are centered within each candidate list before MAE/RMSE. Pairwise
agreement excludes teacher ties within 1e-6. These are teacher imitation
statistics, **not correct-character accuracy or an end-to-end IME metric**.
The low top-1 agreement does not justify adopting this scorer as a teacher
replacement. Finishing the training and recovery pipeline is not a quality pass.

Training windows totaled 123.420 seconds; their maximum recorded peak RSS was
667,032 KiB. These figures include setup and checkpoint work and are **not
inference latency measurements**. The separately frozen matched-list CPU
benchmark below subsequently completed. Neither completion establishes
acceptable ranking quality or product readiness.

## Recovery evidence and limits

Each window ended with preserved model, Adam, RNG and cursor state. Private
backup copies were read back, checked file-by-file, restored into fresh
directories, and loaded in fresh processes for finite synthetic inference.
One execution transport connection dropped; its shell exit status is unknown.
Complete checkpoint pairs, a terminal window record and absence of a running
worker were independently verified before continuing, without a duplicate run.

Backups deliberately exclude frozen third-party embeddings, vocabulary,
examples and raw teacher scores. They require the separately verified external
dependencies; they are **not standalone full-environment snapshots**. No model
weights or experimental examples are distributed with this report.


## Subsequent matched-list warm CPU benchmark

The fixed final student and frozen teacher were measured on the same 319 lists
in eight serial CPU windows. Each list had three measured repeats; each model
had three warm-up lists per window. Model loading, disk access and outer output
checks were excluded. All eight windows completed; no failed samples were
removed. Inputs, outputs, resources and aggregation identities were verified.

| Implementation | Per-list median | Per-list p95 |
|---|---:|---:|
| Original Python/PyTorch teacher, full forward per candidate | 40.545 ms | 57.465 ms |
| Fixed student, including its built-in input/output checks | 2.313 ms | 2.772 ms |

Each condition first contributes the median of its three repeats. Percentiles
then use nearest rank across the 319 conditions. The median **paired**
teacher/student time ratio is 17.24; its p95 is 23.97. This is not a ratio of
unpaired aggregate medians. The teacher baseline does not use a native runtime
or optimized prefix KV reuse. Internal validation overhead differs between
implementations. These are warm Python compute timings, **not equivalent-quality
speedups, startup latency, Windows results or end-to-end IME latency**.

The budget was 8,613 measured teacher candidate forwards plus 108 warm-up
candidate forwards, and 957 measured student lists plus 12 warm-up lists.
Private student outputs captured during these existing calls were reused for
independent metric recomputation and the analysis below; no extra inference
was needed. Top-1 and pairwise agreement matched exactly; recomputed score
errors differed by less than 1e-6 from the original evaluation.

## Descriptive error analysis

On the 85 student-only diagnostic lists, the median score range was 0.406553
for the student versus 1.186031 for the teacher. The median per-list
student/teacher standard-deviation ratio was 0.358069; mean within-list
Spearman correlation was 0.290784. These describe weak score spread and rank
matching, not a proven causal explanation or a tuned calibration rule.

A constant-equal-score reference has centered RMSE 0.392166; the student's
0.375560 is only 4.23% lower in relative RMSE. This is not a 4.23 percentage-point
accuracy improvement. Candidate ordering is construction-dependent, including
the original gold candidate at position zero; order correlations must not be
converted into input features or treated as independent quality evidence.

**Decision remains: do not adopt this scorer as a teacher replacement.** No
additional training, checkpoint selection or diagnostic-based tuning was done.
All analysis remains inside the historical teacher training pool; the final
acceptance set was not opened. Raw per-candidate outputs remain private.
