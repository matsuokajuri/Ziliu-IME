# Ziliu repository instructions

- A `.codegraph/` index is part of the local development setup. Use CodeGraph before text search
  when locating symbols, call paths, or affected tests. Run `codegraph sync .` after structural edits.
- Keep `ZiliuTIP.dll` minimal: no network, user database writes, process waits, or large model loads.
- Keep platform-independent behavior in `src/core` and cover it with deterministic tests.
- Do not modify files inside `third_party/`; use a first-party adapter or `data/ziliu` overlay.
- Treat compiler warnings as errors. New warning suppressions require a reason next to the target.
- Do not register the development TIP automatically. Registration changes Windows user state and is
  an explicit manual verification step.

