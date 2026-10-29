# Sealed evaluation guard (research utility)

This standard-library-only candidate extracts ordering and completeness ideas
from an internal evaluation runner. It has **not** replaced or been integrated
into that runner. Passing synthetic tests is not a real-model evaluation,
Windows acceptance, recovery certification, or permission to publish data.

`verify_then_load(root, seal_sha256, plan_digest, expected_windows, target_loader)`
verifies all listed windows and menus before calling the supplied callback once.
The caller must independently trust the seal digest, plan digest and complete
ordered condition lists. It must prevent target access before this API; the
module cannot detect a target that was already read elsewhere. A hash proves
byte equality, not provenance or target-free generation.

Each condition is `{scene_index, step, mode, context}`. The caller supplies a
list of condition lists, one per window. No actual scene IDs or private target
values belong in source or public fixtures. The explicit on-disk contract is:

- `seal.json`: `{plan_sha256, windows:[{path, sha256}, ...]}`
- `window-0000/receipt.json`: `{plan_sha256, window:0, menus:[...]}`
- `window-0000/COMPLETE.json`: JSON-semantically identical receipt
- Each menu reference adds `{file, sha256}` to the condition fields; `file` is
  a unique basename under that window's `menus/` directory. Each JSON menu
  includes matching condition fields; other payload fields are opaque here.
- Every expected window requires COMPLETE and must have no UNKNOWN marker.

`verify_analysis(value, complete, plan_digest, window, expected)` checks exact
canonical JSON equality, identity and unique complete condition-key coverage.
It deliberately does **not** validate metric semantics, recompute losses,
certify the supervisor/process outcome, or establish absence of training.
Those require separate checks. Expected checkpoint steps and window counts
are supplied by callers rather than copied from any private experiment.

The implementation rejects traversal, empty path segments, duplicate JSON
keys, nonfinite JSON constants, symlink files/parents below the supplied root,
nonregular files, oversize files and observed changes during a bounded read.
It uses Linux/Unix `O_NOFOLLOW` and `O_NONBLOCK`; Windows support is not claimed.
Use a trusted root in a private directory whose ancestors are not writable by
untrusted processes. Stop or coordinate writers. These checks are **not** a
sandbox or a comprehensive defense against concurrent filesystem replacement;
there is no atomic snapshot spanning all checked files and the callback.
No files are modified by the module and callback behavior is caller-owned.

Run invented-fixture tests without any models or datasets:

```sh
python -B -m unittest discover -s tools/recovery/tests -p 'test_sealed_evaluation.py' -v
```
