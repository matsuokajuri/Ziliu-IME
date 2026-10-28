# Local supervised command, source-only candidate

Source independently reviewed with 10 synthetic subprocess tests. Not admitted for real teacher/model execution.
No existing teacher runtime source, pins, approvals or attempts changed.

## Contract

- Exact reviewed argv (no shell launcher), complete replacement environment,
  absolute cwd, supervisor/executable/source byte hashes, stdout/stderr cap,
  deadline <= 120 s, TERM-to-KILL grace <= 10 s. Caller must pin every executable
  script/dependency in its independent closure; this helper is not that closure.
- New private attempt only. Fsynced no-overwrite MANIFEST, REVIEW and INTENT
  precede spawn. PID, Linux boot ID, process start ticks and session/group ID
  recorded immediately after spawn. Spawn-to-PID-receipt crash remains UNKNOWN.
- Separate session/group; before every signal, verify exact own unreaped leader
  identity and group. WNOWAIT keeps leader unreaped until supervision ends,
  preventing PID reuse during group signaling. No recovery signal or replay API.
- Nonblocking stdout/stderr captured in private files with fsync each chunk.
  Per-stream cap <= 4 MiB, overflow is failure; retained prefix, observed count
  and truncation flag explicit. Counts after incomplete EOF are lower bounds.
- Deadline or overflow triggers own-group TERM then KILL after grace. One-second
  post-KILL observation bound; unreaped child/escaped pipe EOF is explicitly
  incomplete/UNKNOWN, not successful termination. No unrelated process signal.
- Output hashes, actual return code, termination reason, capture completeness,
  cleanup evidence and elapsed time sealed in durable terminal receipt. Any
  existing attempt, including terminal, blocks automatic retry. Recover returns
  classification only, never adopts output as model success.
- Actual admitted runner retains its own CPU2/memory4GiB/fresh8GiB/shared lock,
  source/runtime/model-call gates; supervisor does not replace or weaken them.

## Boundaries

Linux /proc, waitid WNOWAIT, process-group and supported local fsync semantics.
Trusted private parent/cwd/source directories with no concurrent mutation are
required; this is not a hostile multiuser filesystem or sandbox isolation tool.
No guarantee against kernel uninterruptible tasks, supervisor death, sudden
storage loss or malicious child escaping session/group. A child that escapes
must not be hunted or killed based on guessed identity. Such cases stay UNKNOWN
and require independent reconciliation; do not automatically launch replacement.
Review approval is a content-bound operational receipt, not cryptographic proof
of reviewer identity. Remote backup durability remains a separate verified step.
If disk failure prevents terminal write, preserve evidence and classify UNKNOWN;
never invent terminal success. Captured outputs may contain private data and must
not be published without separate privacy review.

## Validation

Eight fast stdlib-only synthetic subprocess/fault tests: stdout/stderr/PID/exit,
nonzero exit, overflow, TERM ignored then owned KILL, existing UNKNOWN rejection,
review/hash rejection before output, PID-receipt failure cleanup, and unrelated
process survives owned deadline. No NumPy, weights, real model or network calls.
Independent source and exact real manifest review required before future use.

## Official references checked by coordinating agent

- https://docs.python.org/3/library/subprocess.html — communicate timeout does not
  automatically kill child; buffering unlimited output can exhaust memory.
- https://www.gnu.org/software/coreutils/manual/html_node/timeout-invocation.html
  — status 137 can mean timeout itself was killed and child may still be alive.
  Thus no inference of successful cleanup from a wrapper exit code alone.

## v2 independent findings remediation

Before reaping the exact leader, inspect /proc group members and session IDs;
leader exit plus pipe EOF alone is insufficient. Same-owned-group descendants
trigger identity-checked TERM/KILL, with bounded observation. A remaining zombie
is explicitly UNKNOWN_ZOMBIE_GROUP_RESIDUAL, not a full group cleanup claim.
Terminal distinguishes `cleanup_confirmed_reaped` (leader only) from
`owned_group_clear_before_reap`, and records observed member identities/states.
Each output's EOF begins false and becomes true only on its actual zero-byte
read. Pre-registration or mid-capture exceptions retain false/lower-bound flags.
Ten synthetic tests now include the independent live-descendant reproduction and
injected capture-read failure, plus strengthened pre-registration failure check.
No real teacher/model admission follows automatically from these source fixes.
