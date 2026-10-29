# Fixed ranking objective remaining93 continuation

New independent identity PUBLIC_RANK_REMAINING93_IDENTITY_V1 inherits exact
fork1 parameters, complete Adam26, frozen original public E, and RNG. It does
not modify original fork logical0/1 contract or old survival epoch.

The ranking objective round covers the original97 TRAIN scenes exactly once:
already-run original batch[2] has4 scenes, followed by remaining original order
old[:2]+old[3:] = first1 +23 batches×4 =93 scenes /24 updates. New counters are
logical0..24, Adam26..50, new scene cursor0..93. Complete ranking round has25
updates/97 scenes. These97 were previously trained by the survival epoch, not
new or independently held-out examples. DEV was already observed after the
first four-scene fork; do not claim that the entire round had no interim DEV.
The new93 continuation admits no further interim DEV, only one final evaluation.

Original survival loss +0.001 residual regularizer +margin1/weight1 complete-path
ranking supplement remain unchanged, original AdamW0.001 and clip1. Conditions
average per scene, then scenes equally per batch. No original-ineligible modes
may be silently omitted; ranking-ineligible negative coverage remains explicit.

Six fixed windows each4 updates. Caller must independently bind all raw modes,
contexts, targets and fixed schedule to actual admitted TRAIN material. The
core validates scene/mode schema but cannot detect a dishonestly omitted mode.
Resources require fresh RAM>=8GiB, CPU2, AS4GiB, exclusive heavy lock, child100s
+cleanup10s below parent120s. Real execution is coordinator-only after exact
config/material/source/manifest reviews. No final data or online upload allowed.

Core API:
operation.run(root,identity,batches,start_update,*,prior_pair,
 expected_input_previous_sha256,backup_anchor,restore_input,model,opt,torch,np,
 packed,admission,adapter,ranking,objective,native_factory,contract,checkpoint,
 parent_validate,op)

parent_validate(p) wraps original fork_contract.validate(p,epoch_contract.validate).
restore_input(prior_pair,expected_input_previous_sha256) must restore exact supplied
pair and return original metadata. Initial input=fork1, predecessor=fork0 NPZ hash.
Later input=previous new window endpoint, with explicit actual predecessor hash.
Both callback fields and actual full state/RNG/counters are checked. Initial
migration newlogical0 must exactly preserve inherited state, history starts empty.

backup_anchor is closed status/input_checkpoint_sha256/input_rng_sha256/
restore_review_sha256/head_review_sha256. Status must be
PASS_PRIOR_PAIR_REMOTE_RESTORE_AND_HEAD_VERIFIED. Exact proof semantics and source
hashes remain independent caller-admission obligations, never inferred from a
self-asserted status or only a hash. Every next window requires fresh remote
complete-byte readback + full-state restore + independent review + head closure.

Every update saves immutable new-schema rank93-NNN NPZ/RNG pair with complete
parameter/Adam/RNG seals and precise predecessor, then restores and verifies it.
Each COMPLETE lists all current window pairs and seals, not only endpoint.
First window also contains migration0. NPZ+RNG is not an atomic pair transaction;
partial files persist, are not overwritten, and cannot be called recoverable
until full independent pair audit. Remote capsule must retain all window pairs.

Failure before update, after opt before checkpoint, or after persisted pair is
recorded UNKNOWN with stage. No blind same-root replay is allowed. Complete
window boundaries have a supported exact restore path. Partial-window recovery
requires separate audit of last fully persisted pair, source/state/chain and
uncertain update outcome, followed by a newly reviewed recovery identity/plan
and fresh root. This first core does not implement or claim arbitrary-point
automatic resume. Preserve partial artifacts and earlier backups, never infer
that a missing COMPLETE means the optimizer did not execute. Do not permanently
stall: independent audit can choose a documented safe new recovery plan.

Source and toy tests alone are not real97 execution or remote recovery proof.
Toy parent callbacks/provenance and gate hashes are invented fixtures. The tiny
numeric test verifies exact 24-update continuous vs six restored-window state,
plus migration, corrupt pair rejection/rollback, bool guards, and failure stages.
No actual model/graph/private target is loaded. Parent runs tests only after
review under the resource lease. No test outcome is claimed before execution.
