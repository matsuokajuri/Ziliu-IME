# Synthetic survival-objective scope diagnostic

This small original diagnostic calls a caller-supplied existing `objective.py`
and adjacent `data.py`. These must be trusted project sources: the loader executes
Python. Supply their location explicitly; no workspace paths are embedded.

```
python diagnose_objective.py --objective /path/to/objective.py
python diagnose_objective.py --objective /path/to/objective.py --tensor
```

The first command uses only the standard library and checks selection logic. The
second additionally calls the unchanged Torch `condition_loss`, backpropagates
through four invented residual values, and makes **no optimizer update**. The
caller must enforce its resource lease before importing Torch. This script is not
itself a memory or wall-clock supervisor.

Two hand-built graph/search records represent the synthetic target `ab` and wrong
candidate `cd`. The proper frontier is unsaturated. When the target is in the
terminal pool at rank 2, the existing objective skips the terminal comparison:
there are no selected survival comparisons, zero regularizer at zero residuals,
zero tensor loss and zero gradient. When the target is absent, a deliberately
saturated invented terminal frontier causes one selected comparison, tensor loss
3 and gradient L1 4. Search records are invented ABI fixtures, not output from a
native decoder run; this does not certify decoder behavior.

This establishes the scope distinction: **candidate survival is not Top1
ranking**. It does not establish a defect, prescribe a new loss, tune a model,
or claim any quality improvement. No dataset, private input, checkpoint, model,
embedding, vocabulary, native library or final acceptance data is opened.
