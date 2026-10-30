# First-TRAIN4 read-only parameter-gradient diagnosis

This deliberately small diagnostic fixed the original training schedule's first four scenes before measuring gradients: 16 existing mode/context conditions at each of parent25, control50 and ranking50, 48 observations total. No selection by gradient or development result occurred. The earlier full-TRAIN centering check does **not** make these four-scene parameter-gradient findings representative of all TRAIN.

We recomputed differentiable forwards and verified bitwise agreement with already sealed residuals, reused the original sealed search, and measured the **original survival objective plus its weighted regularizer**. The ranking50 label names an endpoint; this diagnostic does not measure the added ranking-loss gradient or ranking-versus-survival conflict.

| Existing endpoint | Positive survival conditions | Positive survival with zero parameter gradient | Survival/regularizer direction conflicts | Largest weighted-regularizer / survival gradient norm |
| --- | ---: | ---: | ---: | ---: |
| parent25 | 10/16 | 0 | 8 | 0.0249% |
| control50 | 10/16 | 0 | 8 | 0.2719% |
| ranking50 | 10/16 | 0 | 8 | 0.1230% |

Every positive survival condition in this fixed sample had a nonzero actual parameter gradient. Although opposing component directions occurred, the weighted regularizer was small relative to survival in these conditions. These observations do not support complete parameter-gradient disappearance or large regularizer cancellation as the immediate explanation on this sample; they do not demonstrate useful optimization steps or explain held-out quality.

Twelve invented analytic-gradient cases passed on the pinned CPU Torch version before actual gradient execution. For all 48 observations, the original total scalar matched its component decomposition exactly, and total gradients matched component sums at predeclared atol1e-6/rtol1e-5 (largest absolute coordinate discrepancy about3.81e-6; relative tolerance also applies). Every condition preserved parameters, Adam state, frozen embedding, Python/NumPy/Torch RNG and untouched parameter `.grad` fields. No optimizer step, checkpoint write or native search occurred. Only existing endpoint identity metadata—not DEV examples/menus—was read for restoration; no final data was read. Supervisor completed and confirmed cleanup.

**Limits:** this is TRAIN-only Linux research on four already-trained scenes, not new quality evidence, a global-gradient claim, final acceptance or Windows product acceptance. Local gradient conflict is not established causal failure. No objective/regularizer change is justified by these observations alone.
