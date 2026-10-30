# Fixed matched-epoch continuation: first control update

A new predeclared experiment continues the existing survival-only control and ranking checkpoints from global Adam step 50 to step 75. Each arm is scheduled for the same fixed 97-scene TRAIN schedule and exactly 25 additional updates, with unchanged objectives and weights. The protocol interleaves single-update windows and requires remote recovery closure before proceeding. After both terminal endpoints are ready, the plan is one fixed comparison per endpoint on the already-used development set. That evaluation has not run; this is not fresh held-out or final acceptance.

## Actually completed

The control arm completed its first update: new logical update 1, global Adam step 51, four TRAIN scenes and sixteen mode/context conditions. The control objective did not calculate a ranking term. The old step-50 checkpoint was preserved. The migration checkpoint exactly preserves its parent parameters, Adam state and RNG; the new checkpoint preserves the frozen embedding and deterministic-update RNG. The supervised training process exited successfully and its owned process group was cleared.

Both the migration and updated checkpoint pairs were privately backed up. The complete official remote readback matched the reviewed archive byte for byte. In a fresh numerical process, both pairs restored all 23,232 trainable parameters, all eight Adam parameter-state groups and RNG exactly. That recovery process made no optimizer updates, model forwards or native search calls.

## Completed recovery boundary

The control window's full recovery gate is **closed successfully**: the recovery-head update succeeded, its complete official readback matched the reviewed candidate, and independent review confirmed the checkpoint/archive/restore evidence chain. Earlier recovery history was preserved. This note is a snapshot of the completed first control window only; it does not report the ranking arm's execution status or any subsequent window.

## Validation and limits

The invented recovery tests now pass all 52 cases, including strict full-state/RNG equality at every step of continuous versus checkpoint-restored execution for both arms. An earlier attempt had one strict failure. Inspection found that its reference used a single-loss shortcut rather than the declared condition/scene/batch computation graph. The test reference was corrected; training code and exact equality requirements were not relaxed, and the original failure was retained. This is not an exhaustive floating-point root-cause claim.

No development-set or final-set evaluation was performed in this continuation window. Training completion and recoverability do not establish better candidate ranking, input quality or a releasable model. This is Linux research and recovery engineering, not Windows product acceptance. No model weights, raw training inputs or private recovery metadata accompany this public note.
