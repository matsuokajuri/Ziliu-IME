# Training-only fit sanity: fixed 256-update result

This experiment asks whether the unchanged tiny scorer and optimization path
can fit a small set of known teacher labels. It does **not** evaluate
unseen examples, independent teacher generalization, Windows behavior or
end-to-end input-method quality. The scorer is not adopted for product use.

## Frozen design

- The first eight scenes from the existing frozen training pool: 22 candidate
  lists, 198 scores. No new teacher calls were needed.
- Same architecture, frozen embedding, seed, loss and AdamW settings as the
  earlier fixed 48-update experiment. Initial trainable arrays were checked
  bit-for-bit against that experiment's step-zero checkpoint, not resumed
  from its trained model.
- Every update used all eight scenes, equally weighting scenes and then
  conditions within a scene. Exactly 256 updates were run.
- Reports at steps **0, 32, 128 and 256** were fixed in advance. All are shown;
  there was no early stopping, checkpoint selection or diagnostic-driven
  tuning. Candidate position was not supplied as a model feature.
- The student diagnostic score cache, development artifacts and final
  acceptance set were not used in this experiment. The original training
  source was hashed/parsed for identity, with only the fixed eight-scene
  subset selected for model work.

## All registered reports, on the same training lists

| Update | Centered RMSE | Teacher top-1 agreement | Pairwise agreement | Median student/teacher score standard-deviation ratio |
|---:|---:|---:|---:|---:|
| 0 | 0.414925 | 0.00% | 44.95% | 0.001882 |
| 32 | 0.307707 | 45.45% | 74.24% | 0.476553 |
| 128 | 0.128846 | 81.82% | 87.75% | 0.952764 |
| 256 | 0.067184 | 100.00% | 92.55% | 0.988156 |

Centered RMSE removes each list's mean score. Pairwise agreement excludes
teacher ties within 1e-6. The final mean within-list Spearman correlation
was 0.925. These are imitation statistics on **the examples being trained**,
not correct-character accuracy. A 100% teacher top-1 agreement here is not a
100% input-method accuracy claim.

The fixed model/optimization path can learn these small training lists and
recover much of their score variation. This makes an entirely disconnected
learning path unlikely for this setup. It does not isolate the cause of the
earlier larger-pool failure, establish sufficient capacity for that pool,
or demonstrate generalization. The earlier 48-update experiment's low
student-diagnostic fidelity remains a valid negative result; this sanity
check does not overturn it.

## Execution and recovery checks

All 32 training windows completed with a fresh memory gate, two CPU threads,
a 4 GiB process address-space ceiling, a 240-second window alarm and a
shared nonblocking heavy-work lock. Window durations totaled 188.096 seconds,
including setup/checkpoint work; maximum recorded peak RSS was 667,656 KiB.
These are not inference-latency figures.

Each eight-update window was followed by a minimal private state backup,
remote byte/hash readback, fresh-directory restoration and fresh-process
model/Adam/RNG loading with finite synthetic inference before the next window.
A separately reviewed two-phase coordinator made uncertain work fail closed;
no automatic retries or overwrites occurred in this run. All 256 updates
were contiguous. Registered-report boundaries additionally preserved verified
control records needed to locate and validate the backups after workspace loss.

Private backups contain only own-trained state and required original source,
hash references and aggregate records. Frozen embeddings, vocabulary,
candidate inputs and raw scores are excluded. Existing verified external
dependencies are still required; this is not a standalone environment image.
Older verified backups remain preserved. No weights or raw experimental
examples are distributed in this repository.
