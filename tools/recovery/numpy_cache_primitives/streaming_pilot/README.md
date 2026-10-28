# One-scene streaming pilot interfaces

Original Ziliu source-only research utilities under the repository GPL-3.0 license.
No third-party implementation code, model weights, tokenizer, dataset, actual
scene identifiers, scores, private receipts or execution approvals are included.

The planner preserves all four original condition pools of one target-free TRAIN
scene while deduplicating identical teacher inputs. Exact existing NumPy numeric
key compatibility is tested against `../contract_numpy_v2.py`. Pure transaction
interfaces account finite operations and block UNKNOWN outcomes; complete-label
and next-update gates bind independently sealed current-plan/state/receipt hashes.

Run the **13 stdlib-only fake-data tests** from this directory:

```sh
python3 -B -m unittest -v
```

These tests do not import NumPy, load a model or select a real scene. They do not
prove teacher quality, real-data training, remote I/O, stochastic recovery or
Windows acceptance. Fake receipt/hash tests verify branch logic only; a real
independent verifier must supply trusted admission and evidence.

No durable scheduler, actual backup uploader, CAS service, production resource
runner or complete real-data provenance loader is implemented here. This is
source recovery and reviewable interface work, not a training launcher. See
PLAN.md for exact scope, pending integrations and retained safety boundaries.
