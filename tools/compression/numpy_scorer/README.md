# Independent NumPy character-transformer scorer

Research code, not an IME product or a production teacher-cache implementation.
`scorer.py` is an original NumPy rewrite with injected NumPy and caller-supplied
read-only FP32 weights. It has no loader, downloads, model weights, vocabulary,
private input, raw scores, telemetry or cache writer. Architecture: 8192 tokens,
6 layers, width 192, 4 attention heads, context bound 64, tied output embedding.
`score(prefix, tail)` performs a full causal forward, selects suffix prediction
rows, normalizes all 8192 vocabulary logits, then averages target log probabilities.
The FP32 reductions, BLAS accumulation and explicit erf-based GELU are a **new
numerical implementation**, not bitwise-equivalent original Torch A.

## Tests

From this directory, using an existing Python environment:

```sh
python -B -m unittest test_scorer_static -v
# Explicit opt-in after resource/runtime admission; uses NumPy already installed:
python -B test_scorer_numpy.py
```

The first command is standard-library only. The second runs eight small, fixed,
public synthetic array fixtures (linear, two layer norms, GELU, uniform and causal
softmax, log-softmax, head layout). It does not load or run a transformer model.
No dependency installation or environment changes are performed by this package.
The observation hook is not a security monitor. This standalone package does not
include the private execution wrappers, runtime checks, reference data or models.

## Historical qualification, 2026-10-28

These are independently reviewed Linux research observations for the same scorer
mathematical body, not results from running this newly packaged test harness:

| Scope | Observed result |
|---|---|
| 8 primitive synthetic fixtures | all passed; no model forward |
| 1 fixed candidate | absolute error 4.76837158203125e-7; not bitwise equal |
| 1 complete 108-candidate TRAIN pool | maximum error 1.9073486328125e-6; 79 FP32 bit mismatches; 0 of 5778 pair relations changed |
| 16 fixed TRAIN pools, 1590 pool candidates | maximum error 3.337860107421875e-6; 1127 FP32 bit mismatches; 0 of 80181 pair relations changed |

Fixed comparison tolerances were atol=1e-4, rtol=1e-5, top-set epsilon=1e-6.
The 16 pools include the earlier 108-candidate pool, not 16 entirely new pools.
All exact/epsilon top sets and stable order matched on those pools. The wide run
also made 32 sentinel calls (1622 scoring calls total); all sentinels matched
bitwise. Sentinels and the repeated-pool check were registered after earlier
NumPy observations, not an unseen confirmatory protocol.
These results do not establish full-domain equivalence, final-set quality,
Windows acceptance or production speed. Reported experiment timings included
hashing/journaling and are not inference benchmarks. No formal numeric cache
was published and no original-A cache replacement is authorized by this report.
Runtime mapping snapshots do not prove absence of every transient executable
mapping. Earlier failed Torch runtime gates are not overturned by these results.
The private qualification cannot be reproduced from this source-only package.

## Attribution and license scope

See [NOTICE.md](NOTICE.md). Original project additions follow Ziliu-IME's root
GPL-3.0 license when integrated; architecture-reference Apache-2.0 attribution
is retained separately. No upstream root scripts, weights or corpora are included.
