# Experimental cache and recovery primitives

Original Ziliu-IME Python standard-library components and synthetic tests.
**Source-only research infrastructure, not a production cache or a deployed
failure-recovery system.** These modules have not been integrated with a real
scoring runtime. The included tests do not run NumPy, PyTorch, a model, training,
a real worklist or a final acceptance set. Linux/POSIX tests are not Windows
product acceptance.

## Components

- `contract_numpy_v2.py`: separates token identity from numerical cache keys;
  validates explicit source/runtime hashes and fixed numerical semantics.
  Names describe contracts only; no NumPy dependency or numerical execution.
- `cache_store_numpy_v2.py`: immutable private FP32 payload IO with exact bit
  and hash readback; payload provenance remains the caller's responsibility.
- `recovery_protocol_v2.py`: chained request/completion state machine and
  explicitly reviewed cache-only publication of durable completed payloads.
- `journal_io_v2.py`: exclusive POSIX writer, O_EXCL temporary creation,
  file/directory fsync, atomic hard-link publication and commit watermarks.
- `storage_admission_v2.py`: byte/inode arithmetic and statvfs observations.
- `evidence_store_v3.py`: lossless factoring of canonical checkpoint JSON into
  raw map blobs, a fixed closure and fully bound per-checkpoint references.
  Reconstructs exact original bytes including an optional final newline.

## Run the synthetic tests

Requires Python 3.12 and a POSIX filesystem supporting the used dirfd,
O_NOFOLLOW, flock, hard-link and directory-fsync operations. No installation
or external package is needed. From this directory:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s . -p 'test_*.py' -v
```

Tests use temporary directories and owned short-lived subprocesses, including
intentional process exits and injected write failures. All token arrays,
identities, scores and mapped-file paths in fixtures are synthetic. Arithmetic
test counts are deterministic budget examples, not recorded input data.

## Safety boundaries and unimplemented integration

- Unknown issued requests are not automatically retried. External process
  observations and independently verified provenance are required. Missing
  files, an exited process or a checksum alone do not prove no request ran.
  Use the journal adapter's conservative classification; the lower-level pure
  classifier assumes externally trustworthy observations.
- Durable completed recovery requires an explicit exact review and published
  evidence binding. A caller-provided approval object is not a security
  boundary or proof that a human independently reviewed evidence.
- No remote backup, upload, scorer launcher, automatic cleanup or deletion is
  provided. Existing corruption is rejected, not treated as a cache miss.
- Partial/orphan evidence is retained and blocks further appends until explicit
  reconciliation. Loss of an entire trailing event plus its commit cannot be
  disproved without an independently retained external head.
- Use one bounded execution window per journal/evidence root. Inventory scans
  grow with references; complete verification reads each expected checkpoint.
  `_clean` checks inventory/readability, not every old reference's full semantic
  binding. `verify_all` provides the complete expected-sequence readback.
  Scalability and extra IO must be measured in a separately reviewed pilot;
  a single full-dataset root is not qualified.
- Storage defaults are proposed caps, not a capacity guarantee. statvfs does not
  reserve bytes/inodes or prove quotas. The budget covers its specified record
  layout only; callers must include other artifacts, emergency handling and
  duplicate attempts. Pre-issued resource gates are not connected to a real
  runtime here. Formal budgets must pin the exact cap digest.
- Exact evidence reconstruction is not executable-map safety validation. An
  independent guard must still evaluate reconstructed checkpoints; runtime
  thread/precision/security enforcement is outside these libraries.
- Synthetic interruption tests do not demonstrate power-loss safety on all
  filesystems, cloud persistence, cross-host restore or Windows compatibility.
  These modules do not defend against a malicious actor replacing ancestors
  concurrently; an isolated owned root and serial execution are prerequisites.

`SOURCE-MANIFEST.json` records this candidate's original source byte/AST hashes;
it does not contain experiment inputs or authorize numerical execution.

## Tiny-pilot helper increment (not a runtime runner)

`pilot_adapter_v1.py` adds a pure full-projection contract check, metadata-only
primary/repeat/sentinel schedule, one-operation journal/staging callback adapter,
exact FP32 repeat comparisons and a TRAIN-only occurrence accessor. The added
13 synthetic tests bring this directory's discovery suite to 74 tests.

The adapter does **not** select real inputs, derive or validate real numerical
keys, load a model, certify a production cache, or export training labels. It
accepts injected callbacks for testing; a separately reviewed fixed runtime
wrapper must pin their implementation and enforce true score/load/getter
budgets. `model_calls=1` describes the intended one callback, not a bound on
arbitrary callback internals. A numerical binding's valid shape is not proof
of provenance. Geometry/repeat helpers require a complete admitted schedule.

A durable completed payload precedes the after-guard, preserving recoverable
bits if that guard fails; those bits are **not** thereby qualified or promoted.
Unknown issued requests are never retried by the helper. It requests a fresh
storage gate before issuing, but actual statvfs, resource limits, hard deadlines,
full checkpoint sequence verification and fresh-process cache-only validation
remain wrapper responsibilities. DEV rows are rejected by the TRAIN accessor;
upstream must verify the full worklist and preserve all candidates/OOV rows.

No private pilot plan, exact experiment source pins, real worklist, token IDs,
scores, weights or execution admission is included. Existing CI test discovery
automatically includes the added tests without a duplicate workflow step.
