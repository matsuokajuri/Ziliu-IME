# Full fixed TRAIN97 frontier diagnosis — gate not adopted

The read-only follow-up covered all 97 previously trained scenes in their original fixed order: 364 conditions per checkpoint, 1,092 native searches across three checkpoints. The first four scenes' 48 searches were reused rather than repeated; six bounded windows added 1,044 searches. No optimizer update, DEV rerun or final-set read occurred. These are training-set diagnostics, not held-out quality or Windows acceptance.

| Checkpoint | Top-1 / 364 | Target coverage / 364 | Graph-reachable / 364 | Covered but positive survival hinge |
|---|---:|---:|---:|---:|
| Parent, 25 updates | 50 | 158 | 364 | 10 |
| Survival-only, 50 total updates | 58 | 163 | 364 | 11 |
| Survival + ranking, 50 total updates | 56 | 166 | 364 | 14 |

Relative to matched-update survival-only, ranking gained 0 and lost 2 Top-1 conditions; it gained 4 and lost 1 covered-target conditions. Target rank improved in 32, worsened in 31, and was unchanged in 301 conditions. Every absent target still had positive survival supervision; no absent-target zero-hinge or reachable-with-no-selected-boundary case was observed.

A path-level follow-up reconstructed the actual candidate-node chains and compared each positive constraint's positive edge prefix with the retained target paths. All covered-target positive-hinge cases were on those actual retained paths. Off-retained-path conditions were zero in every arm. Thus this real TRAIN diagnostic did not reproduce the earlier synthetic alternate-segmentation counterexample. It instead observed survival margin constraints on already retained paths. The experimental coverage gate removes such margins too; these findings do not justify adopting or training it on the earlier hypothesis. Existing checkpoints and objectives remain unchanged.

The new pure-Python path classifier passed eight synthetic tests. Its actual classifications were cross-checked against an independently implemented reverse-node-chain prefix check over all 1,092 sealed menus. “Retained paths” means representative paths actually present in the native pool, which deduplicates candidate text; this is not enumeration of every theoretically possible segmentation. Off-path classification alone would not establish a bug or causality.

All six new windows completed cleanly under the existing two-CPU / 4 GiB process limit and fresh-memory gate. Each window's menus were sealed before target analysis. Independent reviews checked complete denominators, pairwise metrics, source/checkpoint/RNG immutability and classification. A window-size assumption was caught before any execution, corrected from an assumed last short window to the actual first short window, and covered by a regression test; obsolete unapproved configurations were retained rather than silently overwritten.
