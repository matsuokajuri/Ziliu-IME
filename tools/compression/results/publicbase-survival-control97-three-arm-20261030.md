# Matched-update survival-only control: fixed DEV diagnostic

This is a single descriptive experiment on the same known, repeatedly used 63 development scenes (252 correlated conditions), not blind validation, the final 10,000-item acceptance set, or Windows product acceptance. The control was declared after observing the ranking arm's result. No model has been adopted or released based on this comparison.

The two continuation arms start from the identical parent checkpoint, optimizer state and RNG state. Both use the same 97 training scenes in the same actual batch order, with 25 additional optimizer updates. The survival-only control omits the ranking term. All control training completed before its only endpoint DEV evaluation; no intermediate control checkpoint was evaluated or selected.

| Arm | Top-1 / 252 | Target retained / 252 | MRR (absent = 0) |
|---|---:|---:|---:|
| Parent, 25 updates | 24 | 94 | 0.146687311 |
| Survival-only control, 50 total updates | 32 | 90 | 0.168881297 |
| Survival + ranking, 50 total updates | 31 | 92 | 0.169373269 |

Ranking versus the matched-update control gains 3 and loses 4 Top-1 conditions, gains 2 and loses 0 retained-target conditions, and improves 28 / worsens 13 / leaves unchanged 211 target ranks. Its MRR advantage is approximately 0.000492.

Thus, this comparison does not show a Top-1 advantage from adding the ranking term. The earlier improvement from parent to ranking cannot be attributed solely to that term: extra survival-only updates also improve Top-1. Ranking retains two additional targets relative to control, while both continuation arms retain fewer targets than the parent. These tradeoffs require further research; they are not evidence of generalization or a release qualification.

Verification: 34 bounded control DEV actions completed; all 504 parent/control menus were sealed before target analysis. A separate three-arm comparison sealed all 756 existing menus before selecting the same 63 targets. Independent recomputation matched all three arms and paired counts, with source/checkpoint/RNG immutability checks. Parent and ranking menus were reused, not regenerated. No training occurred during evaluation and the final acceptance set was not read.
