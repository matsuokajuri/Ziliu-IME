# Independent one-batch ranking fork

This source is a new experiment, not an update silently inserted into the old
fixed-97 epoch. No real training is authorized by this source package itself.

## Fixed proposal

An independently reviewed runner may restore the existing exact step25 parent
parameter/Adam/embedding/RNG state and create namespace
`PUBLIC_BASE_RANK_FIXED_BATCH_V1`. Selection is the original parent schedule's
third batch (`schedule[2]`), exactly four previously fixed TRAIN scenes in order.
All original eligible modes and both empty/given contexts must be present.
Mode completeness and exact private graph bytes remain caller-admission duties;
the core cannot detect an omitted mode if the caller misdeclares its material.

The original survival loss and 0.001 residual regularizer remain. Ranking margin
1 and weight1 are a fixed experimental starting point, not DEV-selected values.
No-negative ranking conditions retain their original loss with explicit coverage
accounting. Original-ineligible conditions fail the whole declared batch; they
are not silently omitted. Conditions are averaged within each scene and then
scenes equally within the batch. Original AdamW learning rate0.001, betas(0.9,
0.999), epsilon1e-8, weight decay0, and gradient clip1 remain unchanged.

## Separate counters and durable state

- Migration checkpoint: logical update0, new scene cursor0, global Adam step25,
  empty **new-experiment** history. Exact parent state and RNG must match identity.
- Single update checkpoint: logical update1, new scene cursor4, global Adam step26,
  exactly one new history entry. Old25-entry history is not relabeled or extended.
- Identity binds the full old identity, original pair hashes, full state/RNG
  digests, fixed schedule, new objective, source and selection-review hashes.
- NPZ arrays contain finite FP32 parameters and complete finite Adam slots;
  variance is nonnegative and every Adam counter must equal25 or26 as appropriate.
- Frozen embedding is not bundled as a weight; its exact hash remains bound.
  External original embedding/vocabulary and qualified runtime are still required.
- RNG sidecar binds NPZ file hash, fork identity and all counters. Restore checks
  every array and seal, metadata, external predecessor, optimizer group and RNG
  via isolated generators **before** applying anything. The final application
  has model/Adam/global-RNG rollback on exceptions.
- Files are immutable and locally fsynced. NPZ plus sidecar is **not** a single
  atomic filesystem transaction: sidecar failure can leave an orphan NPZ.
  The enclosing operation marks UNKNOWN and never retries or overwrites it.

## Exact recovery and permission boundaries

First restore the parent25 pair with the unchanged parent restore implementation.
Confirm its exact identity, pair hashes, step25/cursor97, full state and RNG.
Create the fork0 pair; restore it against expected predecessor=parent NPZ SHA.
After the one permitted update, create fork1 and restore it against expected
predecessor=fork0 NPZ SHA. Preserve the original pair and both new pairs.

A private recovery capsule must include both fork NPZ/RNG pairs, identity, exact
restore source, source/runtime pins, parent provenance and independent evidence.
It must exclude private input/target text, raw graphs, teacher weights and other
unlicensed artifacts. External embedding/vocabulary/runtime prerequisites must
be listed and retained. Remote byte readback plus a fresh full-state restore
and independent review is required before declaring durable recovery. A local
COMPLETE deliberately says REMOTE_BACKUP_REQUIRED and forbids another update.

Caller supervision must freshly check available RAM>=8GiB, CPU2, AS4GiB,
exclusive heavy-work lease, and a bounded single window (proposed100s plus10s
cleanup). Real execution needs a new exact reviewed config/material binding and
single coordinator launch. Failures retain UNKNOWN; do not switch samples or
replay uncertain work. No DEV/final model selection or Windows acceptance is
implied.

## Synthetic coverage

`python test_fork.py --original-operation original_operation.py` uses a tiny
invented linear module and a synthetic parent-validation callback. It does not
load the real model or private data. Test initial optimizer state is deliberately
marked global25; this is not a historical training claim. It verifies logical
counters, full migration/update restores, malformed/tampered inputs rejected
without partial mutation, injected application rollback, immutable output,
complete one-batch lifecycle and failure/UNKNOWN/no-replay behavior.

Run tests only under the caller's resource lease. Passing toy tests is necessary
but does not authorize or certify the real-data fork.

Gradient buffers are not checkpoint state. The operation calls `zero_grad` before
its update; callers resuming elsewhere must do likewise. Rollback handles Python
exceptions, not power loss or abrupt process termination.
