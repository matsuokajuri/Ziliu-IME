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
inference latency measurements**. A separate matched-list CPU latency protocol
is pending; the overall experimental plan is not yet complete.

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
