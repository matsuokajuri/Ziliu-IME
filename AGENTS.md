# Ziliu repository instructions

- A `.codegraph/` index is part of the local development setup. Use CodeGraph before text search
  when locating symbols, call paths, or affected tests. Run `codegraph sync .` after structural edits.
- Keep `ZiliuTIP.dll` minimal: no network, user database writes, process waits, or large model loads.
- Keep platform-independent behavior in `src/core` and cover it with deterministic tests.
- Do not modify files inside `third_party/`; use a first-party adapter or `data/ziliu` overlay.
- Treat compiler warnings as errors. New warning suppressions require a reason next to the target.
- Do not register the development TIP automatically. Registration changes Windows user state and is
  an explicit manual verification step.

## VM ownership: do not hand VM work to the user

- Codex owns the test VM's entire operational lifecycle: availability, login-session readiness,
  isolated control, testing, reboot recovery, evidence transfer, cleanup, and safe final state.
  Never ask the user to open VMConnect, unlock or log in to a guest, enter guest commands, restart
  a VM, take screenshots, copy files, or collect evidence merely to continue Codex-managed tests.
- Before rebooting or signing out of a VM needed for further GUI testing, prove an unattended
  post-reboot path to an **unlocked interactive desktop** and working screenshot/input control.
  A reachable endpoint, running process, or `query user` showing `Active` is not that proof.
  If this preflight fails, do not reboot into a predictable manual-unlock dependency; reorder the
  tests or choose another safe, in-scope automated path.
- If the guest is locked or control fails, perform bounded diagnosis and use only an existing,
  authorized recovery path. Do not bypass Windows authentication, guess or extract credentials,
  weaken the lock screen, enable autologon, or modify host/guest security settings merely to
  evade the boundary. Do not repeat the same failed recovery attempt without new evidence.
- If no authorized automated path exists, mark the affected VM acceptance gate `BLOCKED`, preserve
  the VM and evidence in a documented safe state, and report the exact boundary. Do not claim PASS,
  and do not turn the report into a request for the user to perform VM operations. Only a later
  explicit user offer may make user-assisted recovery an option.

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
