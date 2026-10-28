# Streaming student admission contracts (source-only)

Original Ziliu research code, GPL-3.0; see LICENSE. No third-party implementation
code, pretrained embeddings, weights, vocabulary, private paths, input records,
real labels, checkpoint bytes or private execution manifests are included.
Python standard-library APIs and mathematical interfaces are used. The AdamW
implementation derives from this project's separately published `numpy_student`
package, with only the learning-rate profile changed from toy 0.03 to proposed
0.001. That new profile is not qualified for real training.

This is an injected one-scene, four-complete-pool, one-update interface, **not a
numerical launcher or production IME**. The H128/D192 initializer proposal has
288,769 trainable float64 coordinates; no real initialization or training is
performed by importing this package or by these tests.

## Standard-library checks

Run from this directory in the complete repository:

```sh
python3 -B -m unittest discover -s . -p 'test_*.py' -v
```

37 source/fake tests pass, including 24 delayed digest-callback mutation
subcases. These do not import NumPy, draw random numbers, read real labels or
embeddings, contact remote storage, train a model or prove Windows acceptance.
Tests use the existing sibling `../numpy_student`, `../context_training_loop`
and `../../recovery/numpy_cache_primitives/streaming_pilot` source packages.
Only those three public test import paths differ from the research source;
implementation bytes are unchanged.

## Contracts and limits

* Real and synthetic namespaces are separately admitted; a payload cannot
  choose its own authority. No DEV or final-set evaluation path is supplied.
* Complete ordered pools, exact labels and materialized payloads must bind to
  externally sealed provenance. Unsupported candidates cannot be silently
  dropped or assigned fabricated scores.
* Genesis and each update require actual remote byte readback, fresh-process
  full-state restore and recovery-head CAS evidence before continuation.
  Structural receipt validation does **not** establish that external I/O ran.
* The reviewed transaction loop is wrapped with a post-digest anchor check,
  including its final callback immediately before issued publication. Drift of
  backed state/tip/source/policy/scope/contract/head is rejected. Failure inside
  loop update leaves the session poisoned and cannot be automatically retried.
* This is an in-process contract guard, not a security sandbox. Admitted source
  must be trusted/pinned and calls serialized; concurrent input/state mutation
  or arbitrary malicious Python is outside its guarantees.
* Numerical runtime/RNG closure, frozen-embedding provenance, actual model
  behavior, resources, remote backup coordinator and independent materialization
  review remain separate prerequisites. No uploader or automatic training
  launcher exists here. Source tests alone grant none of those permissions.

The frozen research resource policy is CPU2, owned address space <=4GiB, fresh
available system RAM >=8GiB, serial heavy work and model windows <=300 seconds.
A separately reviewed supervisor must enforce it; environment variables alone
are not evidence of thread-count or executable-memory safety.
