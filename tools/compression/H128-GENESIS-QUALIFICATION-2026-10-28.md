# Full-size student initialization — bounded Linux research result

A separately reviewed D192/H128 initialization completed normally in 1.538 seconds on 2026-10-28. This is a step-zero state, not trained-model progress or Windows product acceptance.

## Actual verification

- 288,769 trainable float64 coordinates: 288,640 uniform outputs and 129 exactly zero head/output-bias coordinates.
- Two optimizer-moment mappings, each with 288,769 exactly positive-zero coordinates.
- Complete optimizer configuration and step zero, full generator state before/after initialization, seed, initialization profile, draw count, and zero update/batch cursor.
- Complete serialized state: 13,863,908 bytes. Every parameter and optimizer-moment value passed finite-value and shape checks.
- Independent result review checked 18 receipt hashes, 8 runtime checkpoints, two native thread-count queries returning 2, source identities, full stream EOF, and owned-process-group cleanup. Both output streams were empty.

The window used two CPU cores, an owned address-space limit of 4 GiB, a fresh available-memory threshold of 8 GiB, serialized project heavy-work locking, and an external 120-second bound. Exact source/module/map checks bracketed the operation; these are checkpoint observations, not a continuous native-code safety proof.

There were **zero forward calls, backward calls, optimizer updates, or evaluations**. No real embedding table, vocabulary, candidate input, teacher label, DEV set or final acceptance set was read.

## Recovery boundary

This serialized raw state is not the TrainingLoop checkpoint envelope or its checkpoint tip. Its file hash must never substitute for a loop-tip recovery proof. Private remote backup and fresh-process restoration have separate evidence gates. A real loop genesis must also be created, backed up, restored and bound to the recovery head before the one-update session can become ready.

A tiny same-process generator-state replay was qualified earlier, but that does not by itself establish this full state's remote recoverability or model readiness. No trained-model quality, fresh-process NumPy restoration, remote-backup completion, or permission to run a training update is claimed by this report.

This public report contains aggregates and boundaries only. It includes no parameter arrays, RNG states, private paths, raw inputs, or model-derived dependency identifiers.
