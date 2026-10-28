# Local command supervision research helper

Original stdlib-only Linux helper. Run `python3 -B -m unittest test_supervised_command -v` in this directory. Ten tests execute only short synthetic child processes; no model or dataset is loaded.

The helper retains immutable launch intent, own PID/start/session identity, bounded output and terminal status. It checks same-group residual processes before reaping the leader. Residual zombies and incomplete output remain UNKNOWN; no automatic replay. It cannot isolate malicious process escape and does not replace a runner-specific source closure, resource gate or numerical review. See DESIGN.md for the exact limitations and official references.

Private runtime manifests, approvals, process IDs, paths, execution logs, models and input records are not included.

Licensed under GPL-3.0; see LICENSE.
