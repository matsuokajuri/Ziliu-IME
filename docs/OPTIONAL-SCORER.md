# Optional ONNX scorer integration seam

This is an opt-in library and synthetic host-test surface. It is not connected to TSF, IPC,
Rime candidate selection, packaging, installation or the normal Broker startup path.
`ZILIU_ENABLE_ONNX_SCORER` defaults to `OFF`. No model assets are included.

## Ownership and admission

- `src/core/scorer` owns deterministic UTF-8 decoding, full-pool admission, token packing,
  scoped request identity, stable source ordering, duplicate handling and fallback results.
- `src/broker/onnx_scorer` adapts the fixed ONNX Runtime 1.20.1 CPU C API. It never loads a
  model inside the TIP. Build-time opt-in creates a separate static target, not a route.
- A future caller must launch a dedicated bounded worker and explicitly map the existing
  TSF privacy decision. Missing metadata, the IPC restricted flag alone, or an unknown scope
  must never imply ordinary admission. This change makes no TSF/IPC protocol edits.
- Each session has an immutable nonzero `(session, generation)` identity. Every request must
  use that exact scope and a strictly increasing nonzero request ID. A context/privacy change
  requires retiring the old scorer and creating a new generation. The caller must recheck both
  identifiers before applying a result and retain the native source-index selection mapping.
- Privacy defaults to unknown. Restricted, blocked and unknown requests never invoke inference.
  Candidate source IDs are ascending, begin at native source zero, and lie in `[0, 8]`.
- The complete pool must contain 2–9 nonempty valid UTF-8 candidates, each at most 63 Unicode
  scalars. NUL, overlong encodings, invalid surrogates and malformed bytes reject. Candidate OOV
  or context-plus-longest-candidate length greater than 63 returns a whole-pool fallback.
  There is no partial filtering or silent truncation. Context-only OOV maps to token 1.
- The session snapshots its vocabulary and request. No request text is logged or cached. It
  is owner-thread-only and rejects reentrant calls, stale identities and use after close.
  Structural errors throw; runtime/resource/coverage/nonfinite failures return fallback.
  In both cases the caller keeps its native candidates. Fallback scores are empty and ranked
  IDs retain native order. No exception text includes input or asset contents.

## Fixed numerical contract

This adapter accepts one separately reviewed contract: causal selected-parent FP32 scoring,
full 8192-token vocabulary log-softmax, then mean log probability over a candidate's target
scalars. IDs 0/1/2 are padding/unknown/BOS; the external vocabulary maps 8189 unique Unicode
scalars bijectively to IDs 3–8191. Packing uses the causal parent of every target and masks
ragged candidate tails. The graph inputs are `ids`, `parent_flat`, `targets`, `score_indices`,
`score_valid`; output is one finite FP32 score per source candidate. Shapes and dtypes are checked.

The exact selected graph must have been reviewed for these semantics before its manifest digest
is trusted. The structural graph gate is defense in depth, not a mathematical proof or approval.
It accepts IR 10, standard-domain opset 20 and a narrow fixed operator/attribute subset. It rejects
unknown fields/operators/domains, external tensor data, nested graphs, local functions, training
metadata and unsupported tensor features. No custom-op library registration is exposed.

Runtime execution is CPU-only, sequential, two intra-op threads, one inter-op thread, spinning
removed and `ORT_ENABLE_ALL`. Raw output coverage and finiteness are checked before duplicate
canonicalization. Only exact text plus exact token-sequence duplicates reuse the first score;
nonduplicates retain their scores. Equal scores preserve source order; there is no epsilon tie.
A manifest identity does not establish model quality, latency or final acceptance.

## External trust and asset format

`CreateOnnxScorer` takes an absolute manifest path and an independently supplied expected SHA-256.
The expected digest must come from an authenticated installation/approval channel under the
caller's control. Reading it from the adjacent manifest, an untrusted request or the model
package itself defeats the trust boundary and is forbidden. Arbitrary hash-bearing manifests
are not approvals. No default digest, model ID, endpoint or private path is compiled in.

The manifest is at most 4096 bytes, UTF-8 ASCII fields, exact order, LF line endings and a final
LF. It has these eight lines (placeholders are not usable assets):

```text
ziliu-onnx-score-manifest-v1
graph=model.onnx
graph_sha256=<independently reviewed graph SHA-256>
vocabulary=vocabulary.tsv
vocabulary_sha256=<exact vocabulary-file SHA-256>
runtime=runtime-filename
runtime_sha256=<verified official runtime SHA-256>
contract=causal-selected-mean-fp32-v1-ort1.20.1-opset20
```

All three asset filenames are distinct plain ASCII basenames in the manifest directory. Paths,
separators, `..`, leading dots, unknown/duplicate fields and unsupported contracts reject.
The pinned manifest must include the runtime version, graph semantics, vocabulary provenance
and licensing in its independent review record; the file's contract field alone proves nothing.
Exact manifest, graph, vocabulary and runtime bytes are hashed before loading. Limits are 32 MiB
for graph bytes, 1 MiB for vocabulary and 64 MiB for the runtime selected by this format.

The vocabulary file begins `ziliu-scalar-vocabulary-v1` plus LF, followed by exactly 8189 LF-ended
lines: decimal Unicode scalar, TAB, decimal token ID. Scalars must be strictly ascending;
leading zeros, aliases, reserved IDs, duplicate IDs and extra fields reject. Changing the
vocabulary serialization requires a new reviewed manifest digest. Do not commit generated assets.

## Loader and resource boundary

Only explicit canonical local paths are accepted; symlinks/reparse components fail closed.
The runtime file is pinned through an open handle, hashed before native loading, and checked
against actual module and symbol provenance. Linux loads the pinned descriptor through `/proc`;
private `$ORIGIN` dependency layouts are intentionally unsupported. Windows uses an absolute
`LoadLibraryExW` path with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`;
it never changes global DLL-search settings. The caller must supply an official runtime in a
trusted installation directory, including any dependencies, in a fresh worker. This is not a
security sandbox against hostile same-user or privileged native-loader/process tampering.

Resource checks are read-only and never alter the ambient Broker, jobs, affinity or system
settings. They require at least 8 GiB currently available physical RAM and at most two effective
CPU affinity slots. Linux requires existing hard and soft address-space limits no greater than 4 GiB;
Windows requires an existing job process-memory limit no greater than 4 GiB. These are distinct
metrics: Windows job process-memory is committed virtual memory, Linux RLIMIT_AS is virtual
address space, and resident/working-set memory is separately observed. Missing evidence rejects.

A launcher must independently enforce the total owned-process budget (including supervisor),
serial heavy work and a hard wall-clock deadline of at most 300 seconds per model window. Use
headroom below 4 GiB per child when a supervisor is present. The library's session deadline
only discards late results; it cannot interrupt a stuck native call. No worker launcher is
provided or silently attached to the product by this change. Callers must stop admission and
release the worker on pause, cancellation or changed context. Never terminate unrelated apps.

## Building and verification

Normal Windows application builds stay Windows-only. Add
`-DZILIU_ENABLE_ONNX_SCORER=ON -DZILIU_ORT_INCLUDE_DIR=<verified-header-directory>` to build the
separate optional static library. This does not link it into a product executable or package
runtime assets. The C API header must be the verified official 1.20.1 header; nothing downloads.

Focused portable tests use existing CMake/CTest conventions without configuring the application:

```sh
cmake -S tests/scorer -B build/scorer -DCMAKE_BUILD_TYPE=Debug
cmake --build build/scorer --parallel 2
ctest --test-dir build/scorer --output-on-failure
```

The focused project enforces warnings as errors. Opting into `ZILIU_ENABLE_ONNX_SCORER` there
also compiles the platform/ORT adapter and runs synthetic asset-admission tests (Linux requires
existing OpenSSL 3 development files). Normal CTest never loads a runtime or model. An externally
supervised invocation of `ziliu_scorer_asset_tests <absolute-manifest> <independent-digest>`
performs an optional small synthetic-input runtime smoke; the arguments remain outside source.
The graph-gate test also accepts one optional external graph path for a read-only format check.

Linux compilation, deterministic tests and any separately recorded external smoke establish
only those exact stages. Windows compilation, Windows loader/job behavior, TSF/Rime routing,
worker launch/cancellation, model semantics/quality, end-to-end latency, packaging and installation
remain separate acceptance gates. They must not be inferred from Linux results.
