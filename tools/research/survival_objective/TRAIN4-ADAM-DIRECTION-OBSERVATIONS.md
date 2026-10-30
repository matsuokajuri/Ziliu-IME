# Fixed-TRAIN4 read-only Adam direction observations

At the existing parent25, survival-control50 and ranking50 checkpoints, we replayed the same four trained scenes as one hypothetical batch. Each scene averages its four mode/context conditions; the batch averages the four scenes. We predicted two updates per endpoint: O (survival plus weighted regularizer) and C (O plus the unchanged ranking objective). These are six hypothetical batch updates, not 48 executed optimizer steps or necessarily the next scheduled batch.

The out-of-place predictor follows the pinned CPU PyTorch 2.12 implementation: global norm-1 clipping (including clipping epsilon), saved first/second moments, bias correction, single-tensor AdamW with zero weight decay, and FP32 parameter-storage rounding. Eight invented numerical cases passed, including multi-parameter global clipping, existing momentum with zero gradient, and an update rounded away by storage precision. No optimizer.step was called.

| Endpoint | Objective | Survival gradient dot predicted delta | Ranking gradient dot predicted delta |
| --- | --- | ---: | ---: |
| parent25 | O | -0.01584458 | -0.00708491 |
| parent25 | C | -0.01635183 | -0.00822223 |
| control50 | O | -0.04680855 | -0.00941846 |
| control50 | C | -0.04902159 | -0.01679867 |
| ranking50 | O | -0.04615552 | -0.00382741 |
| ranking50 | C | -0.04750112 | -0.00831267 |

All six predicted directions locally decrease both survival and ranking losses to first order; the weighted regularizer locally increases. Thus this tiny replay does not support the claim that Adam necessarily turns the observed component conflicts into opposing survival/ranking loss changes. More negative first-order values are not measured finite-step loss changes, quality gains or evidence to choose a model.

All three batch gradient-addition checks passed the unchanged atol 1e-6 / rtol 1e-5 thresholds. **The previously disclosed single-condition strict coordinate-addition failure remains a failure**; batch checks do not erase it. Predictions use directly differentiated O/C objectives, not sums substituted for the actual gradient.

Forty-eight sealed forward observations matched; original parameters, optimizer state, embedding, RNG and .grad remained unchanged. There were zero optimizer updates and zero checkpoint writes. No DEV examples/menus or final data were read. Endpoint restoration used identity metadata only. The supervised run exited successfully and its owned process group was cleared. Independent review checked source, synthetic cases, scalar aggregation, result identities and directional summaries; it did not independently recompute all real update vectors.

This is a four-trained-scene Linux research result, not full-TRAIN behavior, held-out quality, final acceptance or Windows product acceptance. No model or raw training material is distributed with this note.
