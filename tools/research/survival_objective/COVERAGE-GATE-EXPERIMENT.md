# Experimental target-coverage gate — not adopted

A synthetic alternate-segmentation graph exposed a distinction between target-text coverage and survival of one preferred gold path. The actual frozen native decoder retained the target at rank 25 in a pool of 25, while the unchanged survival objective still imposed an internal-path hinge of 6. This is a scope mismatch with pure coverage supervision, not proof of a decoder defect or a cause of observed quality regressions.

`coverage_objective.py` implements an isolated experiment: when the target text is already present, omit internal survival boundaries; otherwise retain the original objective's selection and arithmetic. Preserve unreachable-target handling and the original regularizer. The trusted original objective is supplied explicitly. No existing objective, trained checkpoint or decoding implementation was replaced.

Verification: six selection tests and seven tensor tests passed. They cover absent-target equivalence of loss and gradient at zero and nonzero residuals, empty valid masks, retained-target regularization and unreachable targets. Runtime qualification is inherited from the existing isolated CPU environment; the small test launcher did not independently requalify all dependency versions. This is correctness evidence for the experiment, not quality evidence.

A separate read-only diagnostic used the first four TRAIN scenes in the pre-existing training order, without selecting by DEV errors. Across three checkpoints and 48 native calls, all three arms had 6/16 target coverage and 2/16 Top-1; every target was graph-reachable. All ten absent-target conditions per arm had positive survival supervision. No covered target had positive survival hinge in this small batch. Thus this TRAIN sample does not demonstrate the synthetic mismatch occurs in practice or justify adopting/training the gate.

All diagnostic menus were sealed before target analysis, and checkpoint/optimizer/RNG/source immutability was checked. No updates, DEV reruns, final-set reads or product acceptance occurred. The gate removes all covered-target survival stability margins, not only alternate-segmentation constraints, so it could reduce robustness. A fixed full-TRAIN diagnostic is the next useful test before any training decision.

Primary background: [Wiseman and Rush (2016), Sequence-to-Sequence Learning as Beam-Search Optimization](https://aclanthology.org/D16-1137/). This motivates distinguishing search survival from final ranking; it does not endorse this project-specific gate.
