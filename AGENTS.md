# Ziliu repository instructions

- A `.codegraph/` index is part of the local development setup. Use CodeGraph before text search
  when locating symbols, call paths, or affected tests. Run `codegraph sync .` after structural edits.
- Keep `ZiliuTIP.dll` minimal: no network, user database writes, process waits, or large model loads.
- Keep platform-independent behavior in `src/core` and cover it with deterministic tests.
- Do not modify files inside `third_party/`; use a first-party adapter or `data/ziliu` overlay.
- Treat compiler warnings as errors. New warning suppressions require a reason next to the target.
- Do not register the development TIP automatically. Registration changes Windows user state and is
  an explicit manual verification step.

## Git workflow

- `main` is the verified mainline and default branch. Keep `archive/*` branches for history only;
  never mix them into new development.
- Give each independent fix one clear commit. With user authorization to push, merge verified work
  into `main`; temporary feature branches count only after they are merged into `main`.
- Before committing, verify the author email is associated with `matsuokajuri` (this repository uses
  `283803707+matsuokajuri@users.noreply.github.com`); do not change the global Git identity.
- After pushing, verify the remote SHA and author attribution. Do not create empty commits, forge
  dates, split commits merely for contribution counts, or rewrite history / force-push without
  explicit authorization. GitHub contribution eligibility is determined by qualifying commits, not
  push frequency.
