# Tiny sequence-score research student

This module supplies a small scorer for an explicitly provided frozen embedding
and exact tokenized candidate lists. It includes no data, vocabulary, model
weights, downloads, training launcher, or production input-method policy.

- 23,041 trainable parameters; external frozen embedding: 8192 × 192 FP32
  (6,291,456 bytes). Total: 1,595,905 parameters, including the frozen table.
  Do not report only trainable bytes as total model memory.
- Shared 192→32 projection, separate context/candidate GRUs, and a 130→32→1
  scorer. Each candidate receives the same scalar computation regardless of
  candidate order. The outputs are **not** normalized language-model log
  probabilities.
- Loss: per-list teacher-to-student KL at temperature 1 plus 0.1 centered-score
  MSE. Teacher scores are detached. Scene/condition averaging belongs to the
  caller and must be specified explicitly.
- No gold-position, candidate-rank, human-label, or old-correctness feature is
  accepted by the scorer.

The caller must verify embedding provenance and hash, token identities and
bounds, teacher checkpoint identity, candidate lists, partition membership and
resource admission. Existing scores from a different teacher must not be
relabelled or silently reused. Keep the exact frozen embedding hash alongside
checkpoints: it is intentionally excluded from `state_dict` and must be
provided independently for restoration.

Example with an already admitted embedding tensor:

```python
from tiny_sequence_scorer import TinySequenceScorer, compression_loss

student = TinySequenceScorer(verified_embedding)
scores = student([2, 15], [[20, 21], [22]])
loss = compression_loss(scores, verified_teacher_scores)
```

Synthetic tests were run with Python 3.12 and PyTorch 2.11.0+cpu on Linux, two CPU
threads, a 4 GiB process address-space limit, and at least 8 GiB available system
memory at launch. They check geometry, frozen dependencies, RNG isolation,
permutation equivalence, bounded inputs, finite gradients, stopped teacher
gradients, offset-invariant loss, and weight roundtrip. They do not establish
compression accuracy, useful ranking quality, latency targets, Windows
compatibility, or release readiness. No trained student is distributed here.

Run `python -m unittest discover -s tests -v` only inside an appropriately
resource-admitted environment with PyTorch already available; this module does
not install it or constrain the caller's process itself.

The per-candidate MLP keeps FP32 operation shapes stable under pool permutation;
mathematically equivalent batched operations need not be bitwise identical.
See [PyTorch numerical accuracy](https://docs.pytorch.org/docs/stable/notes/numerical_accuracy.html).
The distillation objective follows the general teacher/student approach in
[Hinton et al.](https://arxiv.org/abs/1503.02531); implementation semantics use
[PyTorch KL divergence](https://docs.pytorch.org/docs/stable/generated/torch.nn.KLDivLoss.html).
