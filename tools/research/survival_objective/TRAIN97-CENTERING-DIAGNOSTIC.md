# TRAIN-only survival-gradient centering diagnostic

A read-only diagnostic reused all 1,092 sealed condition menus from the fixed 97 TRAIN scenes (364 conditions per existing parent25, control50 and ranking50 endpoint). No new model/native calls or optimizer updates occurred, and no DEV or final-acceptance data was read.

The residual head subtracts each consecutive equal-span valid-edge group's mean. This can structurally annihilate some score differences: a singleton valid group has zero residual. We tested the actual selected survival constraints rather than assuming that synthetic failure mechanism occurred here.

For each fixed selected negative-minus-positive path coefficient vector, exact rational arithmetic computes the transpose-centering projection. Invented unit cases and 1,728 independent basis-difference checks passed before actual data analysis; a separate condition implementation independently checked every actual menu.

| Existing endpoint | Positive hinge pairs | Positive hinge conditions | Positive hinge pairs with exact zero projection | Positive hinge conditions with zero-projection first maximum |
| --- | ---: | ---: | ---: | ---: |
| parent25 | 494 | 216 | 0 | 0 |
| control50 | 449 | 212 | 0 | 0 |
| ranking50 | 453 | 212 | 0 | 0 |

The previously identified retained-target/positive-hinge conditions (10/11/14) likewise had no zero-projection first maximum. Reporting uses a fixed hinge threshold of 1e-9 and descriptive float64 scores; exact centering coefficients use no floating zero threshold.

**Conclusion:** these sealed TRAIN results do not support structural gradient annihilation by group centering as an explanation for the remaining positive survival constraints. No objective or model change is justified by this hypothesis alone.

**Limits:** nonzero edge-score projection does not prove a nonzero or sufficiently large neural-parameter gradient. Shared parameters, Jacobian scale, FP32 arithmetic, discrete detached search and the separate regularizer are not measured here. These are trained-scene diagnostics, not new held-out quality evidence, final acceptance, or Windows product acceptance.
