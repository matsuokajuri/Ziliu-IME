# One-condition ranking admission wiring

Original, injected integration seam. `condition_loss` performs exactly one packed
preparation, model forward and native search; closes the native handle; then calls
the strict path/dual-score adapter and the ranking supplement's combined loss.
It accepts no optimizer and performs no backward, update or file I/O. A caller
owns resource supervision, exact identities and input permissions. Returned
metadata allowlists counts and scalar values, not target text or candidate paths.

`scene_condition_mean` retains the original mean-of-conditions for each scene,
then equal mean over scenes. It rejects missing/ineligible conditions rather than
silently dropping them. It validates only the nested mean mathematics; complete
2/4-condition-per-scene coverage is still a caller contract. It is not invoked to reinterpret a single-condition
admission as complete scene or batch coverage.

Synthetic tests inject fake packing/model/native objects but use the actual
original objective, ranking supplement and path validator. Thus they test wiring
and accounting, not real model or native execution. Tests have explicit source
path arguments and must run under the caller's resource lease.

A separately supervised private check restored the existing checkpoint and
executed one predeclared TRAIN condition under `torch.no_grad`, with no backward
or optimizer update. This is not complete-scene or batch coverage and is not
model-quality evidence. Its safe aggregate result is recorded separately; no
private sample or exact per-sample metrics belong in this directory.

Real use still requires exact material/identity admission, resource supervision,
and before/after parameter, Adam, embedding and RNG seals. The generic source
never grants permission to run a dataset or begin training. No DEV/final access,
product acceptance or Windows claim is part of this seam.
