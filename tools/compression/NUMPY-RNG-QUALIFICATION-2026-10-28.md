# Bounded NumPy RNG research qualification — 2026-10-28

This Linux research result is not Windows product acceptance, a trained model, or a cross-platform randomness guarantee. No model forward, backward, optimizer update, real embedding, candidate input, teacher label, DEV or final-test access was part of these windows.

## Import-only prerequisite

A separately reviewed single `numpy.random` import completed with exit code zero in 0.572 seconds. The pinned runtime's source/module/executable-map checks passed before and after import. There were zero explicit PCG64 constructions or draws; package import may have implicit initialization effects. Earlier incompatible Python typing-wrapper and Cython shared-metatype metadata assumptions were corrected using exact source contracts, without adding unknown modules or weakening anonymous-executable/W+X rejection.

## Tiny state replay

A separate reviewed operation completed with exit code zero in 1.376 seconds:

- 2 explicit PCG64 constructions and 2 Generator constructions;
- 3 uniform calls, producing 20 float64 values in total;
- 5 complete generator-state reads and 1 state write;
- 2 native OpenBLAS thread-count queries, both returning 2;
- 0 model calls or training updates.

The operation saved state after a four-value prefix, generated eight values, restored that state into the second generator, then regenerated eight values. The exact reviewed core asserted equality of all 64 output bytes and the entire final generator state. Successful receipts and source review establish those assertions; the retained result has one final-state snapshot and an output digest, not two independently retained final snapshots.

Independent result review checked 18 receipt hashes, 8 runtime checkpoints, source pins, complete output-stream EOF, and owned-process-group cleanup. Both output streams were empty. Execution used two CPU cores, an owned address-space limit of 4 GiB, a fresh available-memory threshold of 8 GiB, the project heavy-work lock and an external time limit. Runtime checkpoints are observations, not proof of continuous native mapping safety.

## Boundaries and next gates

This establishes only a small same-process, same-installed-NumPy state replay. It does not establish fresh-process restoration, full-size initializer behavior, checkpoint backup safety, real training quality, or Windows behavior. Full-size initialization, complete student/optimizer/RNG/cursor checkpoint creation, independently verified remote recovery, and any training operation remain separately gated. Existing failed or uncertain operation identities are preserved rather than automatically replayed.

Only aggregate counts, timing and limitations are published here. No generator states, private paths, data, model-derived dependency identifiers or weights are included.
