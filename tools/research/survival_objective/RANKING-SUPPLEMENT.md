# Synthetic-only ranking supplement

New original research code, not connected to TRAIN, DEV, or production. The old
objective, optimizer, checkpoint and recovery chain remain untouched.

`ranking_loss(graph, target, residuals, candidate_paths, objective)` validates
explicit complete edge paths from offset 0 to `total`, reconstructs their text,
and excludes **all** paths producing the target text from negative selection.
It selects the highest detached current additive-score different-text candidate
(ties: lexicographically smallest edge tuple), and uses the original coherent
gold-path function. The new term is `max(0, 1 + negative_score - gold_score)`.
Margin 1 and weight 1 are a **fixed synthetic starting specification**, not
DEV-tuned hyperparameters or evidence of a useful real-training setting.

`combined_condition_loss` preserves the original survival plus 0.001 residual
regularizer unchanged and adds the ranking term only when defined. No different
text negative produces `ranking_term=None`, `ranking_applied=False`, and retains
the exact original loss. An unreachable original target remains ineligible;
it is not reported as a successful zero-loss observation. Satisfied margins,
by contrast, are eligible zero ranking losses. No batching, data skipping,
training integration or model selection policy is implemented.

## Search boundary

A previously prepared native-source candidate inspected for this design builds
the terminal pool from terminal-state and best candidates, deduplicates text, and
stores node references. Its identity against the currently loaded native build
has not been re-established here, so it is only an adapter research lead. Pool text alone
is **not** an invertible edge path. This module has no native adapter. Its caller
must prove each supplied complete path belongs to the exact graph/search record,
and must account for any native score offsets or biases before real integration.
This module computes only the existing additive differentiable edge score.

The original gold-path function excludes single full-span edges; this code calls
it unchanged. Candidate validation accepts complete full-span paths as explicit
caller candidates, but this does not assert that native search generated them.
Malformed paths and nonfinite weights/residuals/scores are rejected. Shared
edges naturally cancel in the pairwise gradient.

## Invented tensor tests

```
python test_rank_objective.py --objective /trusted/source/objective.py
```

The adjacent `data.py` is also loaded as trusted project Python. The caller must
apply the CPU2 / memory4GiB / fresh available8GiB / wall-clock resource lease;
this test runner itself is not a resource supervisor. Tests use invented ASCII
graphs, not a model, native library, checkpoint, vocabulary or dataset. They
cover strongest-negative selection, shared-edge cancellation, same-text paths,
missing negatives preserving a nonzero original loss, unreachable gold,
malformed paths, nonfinite values, combined accounting, satisfied margin and ties.

A tensor test compares the same-value result to PyTorch
[`margin_ranking_loss`](https://docs.pytorch.org/docs/2.14/generated/torch.nn.modules.loss.MarginRankingLoss.html).
This reference does not require upgrading the existing CPU runtime.

Passing these tests establishes only mathematical and accounting behavior on
invented fixtures. It is not final acceptance, Windows acceptance, a model-quality
result, or authorization to replace the original objective.
