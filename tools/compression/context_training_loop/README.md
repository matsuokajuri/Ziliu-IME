# Context training transaction interface (source-only)

Original Ziliu research code: a small dependency-injected loop, full-model/AdamW adapter and atomic no-overwrite checkpoint store. The six Python files retain their independently reviewed bytes. Run the 33 stdlib-only fake/static tests with:

```sh
python -m unittest discover -s . -p 'test_*.py'
```

The suite does not import NumPy or execute a real model. Existing synthetic toy learning/resume results are not evidence this new full-model loop has numerically trained or resumed correctly.

- TRAIN batches use exactly four conditions per scene, complete candidate pools, equal scene/condition loss and gradient accumulation, then one AdamW update.
- DEV uses forward-only calls with separate issued/complete evidence; final evaluation records are denied by the sealed provenance policy.
- Checkpoints bind complete parameter/moment/config/RNG/cursor state, schema, source identity, policy and batch schedule. The payload hash includes exact input bytes and TRAIN targets.
- An issued transaction without proven completion blocks continuation. Immutable checkpoints/receipts use fsynced private temporary files and no-overwrite hardlinks; recovery requires external sealed receipt hashes. No automatic replay or orphan adoption.

This is an interface, not a production launcher. A separately reviewed launcher must verify actual source/runtime hashes, trusted loader provenance and payload hashes, a safe private directory, supported local filesystem, serial single-thread calls and resource limits. Do not concurrently mutate inputs/state. File locking rejects separate owners; it is not same-instance thread synchronization. DEV receipts retain only result digests, not recoverable full score outputs. Local atomic publication does not provide remote backup durability.

The adapter expects injected compatible model, GRU, objective, AdamW and NumPy objects. No weights, pretrained embeddings, dataset, input logs, private policies, execution admission or numeric runners are included. Fixed toy AdamW settings are not qualified real-training hyperparameters. No quality improvement, final acceptance or Windows verification is claimed.

See LICENSE. Python standard-library and mathematical interfaces are used; no third-party implementation code is copied into this package.
