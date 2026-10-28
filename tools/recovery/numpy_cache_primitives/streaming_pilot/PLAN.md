# One complete TRAIN scene streaming pilot — source-only candidate

Purpose is pipeline correctness, NOT quality improvement, eight-scene training,
all104 supported scenes, all128 TRAIN scenes or final/Windows acceptance.
Existing streaming v2 design and earlier sources remain unchanged.

## Selection and mapping

A separately audited metadata-only loader must extract *all128 TRAIN* scene
headers and their original four candidate pools from the exact repaired worklist
and census joins, without reading DEV values, gold or final records to select.
This module accepts only target-free TRAIN schemas and rejects labels/extra fields,
rederives support reasons, verifies full mode×condition crossing, original pool
indices, token ranges, exact existing input_id equality and four condition identities.
No new tokenizer or re-encoding: retain supplied canonical prefix/tail bytes.
Context UNK/BOS obey original prefix rule; tail-only OOV restriction is not applied
to context. A regression preserves context UNK while rejecting altered input IDs. Select lexicographically
first scene_id among all complete four-pool supported TRAIN scenes. Future loader
must independently prove full128 input completeness and expected104 eligibility;
this pure fake-tested module does not assert a fabricated real-scene manifest.
It does not materialize or choose an actual scene in this source-preparation step.

Preserve all candidates in each selected pool and their original order. Dedup only
mathematically identical prefix/tail teacher inputs and map every occurrence back
by input ID/cache key. Exact existing fullNumPy input/key schema is retained and
cross-tested with existing contract module; inherit only the *same fully qualified
full_projection numeric contract*, not original TorchA or suffix. Distinct scene
experiment and operation namespaces remain necessary. Caller must independently
validate full teacher contract/source/runtime identity; arbitrary supplied hash
is not itself approval or scorer selection. No data/model are loaded here.

Chunks are configurable up to32 unique scene inputs plus pre/post fixed sentinel
(max34 actual scores). Each chunk accounts one load, two getters, 2*score+6 guard
checkpoints and twice that many maps. Smaller final chunk remains exact. Fixed
sentinel must be independent qualified input and not silently overlap scene input.
No runtime call or sentinel/reference validation is implemented in pure planner.

## Complete-label and transaction gates

- Each scored record needs matching contract/cache/input, exact finite FP32 value,
  CLOSED receipt and externally supplied independent expected record hash. All
  unique inputs must exist; return all four original pools with weight1/4 each.
  Incomplete labels cannot authorize an update; output says no numerical admission.
  No teacher score substitution, pool filtering or topK sampling.
- Cumulative ledger is immutable pure state: finite predeclared per-operation costs
  must sum within complete maximum. Separate counters for score/load/getter/guard,
  retained bytes and inodes; source identity bound. Sentinel/recompute overhead
  must be included in declared costs. Controller must independently derive and
  audit costs from plan plus worst-case retained evidence/log/state/backup budgets.
  Current module cannot measure actual disk or implement resource admission.
- Issued immediately consumes full cost and sets UNKNOWN; no next issue until exact
  externally sealed closure. Explicit recompute requires a prior CLOSED operation
  and matching recomputed payload hash; unknown is never miss or refunded budget.
  No automatic retry/reset, no arbitrary new operation beyond approved spec.
- Genesis or updated local state require independently sealed complete checkpoint,
  remote exact download, fresh-process complete-state restore, and expected prior
  generation/head-hash CAS+readback before next-update gate. Rawteacher/frozen
  dependencies excluded. UNKNOWN remote/restore/head blocks. These are contract
  checks, NOT actual uploader/head API/durable FS/remote verification implementation.
  Attestation hashes must originate from external audit authority, never hashes
  freshly invented by the same unchecked caller; all are pinned in actual admission.

## Remaining integration before materialization or model

Independent source review, actual full128 provenance loader, supported-complete
scene selection freeze and 1-scene identity, fixed exact teacher contract,
cumulative max call/disk/backup budgets, durable no-overwrite intent/ledger CAS,
qualified supervised resource runner, separate numerical student adapter, genesis
remote restore, real teacher label staging/independent review and private backup
adapter. No real scene, label, gold, teacher/model/weights, private upload or source
mutation outside these new files occurred here. Existing CPU2/AS4GiB/fresh8GiB,
8GiB disk reserve, serial heavy lock and <=300s (current120s) gates stay unchanged.

## Source remediation v2

Independent three negative findings retained unchanged. Label assembly now requires
externally expected original plan hash and recomputes whole canonical plan hash,
so dropped/reordered/duplicated pool mapping or altered weights/tokens cannot hide
under an old plan identity (nor a caller-recomputed replacement hash). Sentinel
accepts exact prefix/tail target-free schema only; no gold/score/extra passthrough.
Next-ready requires expected current experiment/source/plan/state schema, complete
checkpoint/state hashes, parent checkpoint and exact cursor. All checkpoint,
remote, restored state and head receipts bind current identity; exact schema and
canonical nested comparisons reject extra fields or bool-as-int cursor changes.
These checks still depend on externally sealed admission/evidence; they do not
prove actual remote I/O or grant materialization/model execution themselves.
