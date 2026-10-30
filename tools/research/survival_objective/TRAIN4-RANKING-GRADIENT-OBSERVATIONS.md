# Same-TRAIN4 ranking/survival gradient observations

We fixed the same original four TRAIN scenes, all mode/context conditions, and the same parent25/control50/ranking50 checkpoints:48 observations. Ranking margin and weight remain1. Candidate paths are validated against the sealed native search; differentiable forwards match sealed FP32 bytes. Ranking is measured hypothetically at parent/control, not claimed to have been their training objective.

All48 ranking terms were positive with nonzero parameter gradients. Both survival and ranking were positive in10 conditions per endpoint. On that explicitly restricted subset:

| Endpoint | Opposing S/R directions | Median norm(R)/norm(S) | Median cosine(S,R) | Median norm(T)/(norm(S)+norm(R)+norm(G)) |
| --- | ---: | ---: | ---: | ---: |
| parent25 | 4/10 | 0.472 | 0.119 | 0.863 |
| control50 | 5/10 | 0.577 | −0.073 | 0.827 |
| ranking50 | 6/10 | 0.507 | −0.076 | 0.801 |

S=survival gradient, R=weighted ranking gradient, G=weighted regularizer gradient, T=actual combined-objective gradient. Direction conflict uses fixed cosine<−1e-9. Ranking is not negligible relative to survival here (ranking50 maximum R/S≈1.060).

**Conflict is not optimization failure:** at ranking50, T still had positive cosine with both S and R in all10 both-positive conditions (minimum0.3053 and0.07684 respectively). This describes local Euclidean directions, not the actual Adam-preconditioned update. These observations alone do not justify changing weights or establish why held-out Top1 failed to improve.

**Numerical qualification—not a clean strict-additivity pass:** the original coordinatewise gradient-addition check (atol1e-6, rtol1e-5) failed on one control condition. An instrumented attempt found one offending coordinate, maximum absolute error1.18738e-6 and maximum excess6.82313e-8. Earlier attempts stopped and their failure records were retained. Before the final48-condition run, policy changed to retain and report this strict failure while collecting the same fixed observations; tolerances were never widened. Final status is `COMPLETE_OBSERVATION_NOT_STRICT_ADDITION_PASS`. Maximum relative-L2 difference across48 between actual total and summed component gradients was≈2.043e-6. Floating-point operation ordering is a possible explanation, not a proven root cause.

Invented analytic-gradient and original ranking-loss tests ran first, including intentionally wrong totals that must remain marked failures. Every actual condition preserved parameters, Adam, embedding, Python/NumPy/Torch RNG and untouched `.grad`; exact forward/scalar, finite-gradient and source-integrity checks remained hard gates. No optimizer step, checkpoint write, native search, DEV example/menu or final-data read occurred. Existing endpoint metadata only was used to restore identities.

This is a four-trained-scene Linux diagnostic with explicit numerical caveats—not complete-TRAIN behavior, held-out quality, final acceptance or Windows product acceptance.
