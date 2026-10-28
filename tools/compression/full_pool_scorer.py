"""Inference-only full-list adapter around the unchanged tiny scorer.

Consecutive chunks of exactly nine (except the final chunk) retain every
candidate in original order. No candidate index is a model input. This adds
no parameters and does not imply DEV qualification or input-method acceptance.
"""
import torch
from tiny_sequence_scorer import TinySequenceScorer


def score_full_pool(model, prefix, candidates):
    """Return all 1..134 raw scores, using ceil(N/9) existing forward calls.

    Caller must explicitly put the model and its submodules into eval mode.
    This function enforces no_grad internally and never modifies model mode,
    parameters or input lists. Validation covers the whole pool before any
    forward call, including the original BOS/OOV/64-token position contract.
    These scores are not probabilities or mean log likelihoods.
    """
    if not isinstance(model, TinySequenceScorer):
        raise TypeError('TinySequenceScorer instance required')
    if any(module.training for module in model.modules()):
        raise ValueError('explicit model.eval() required for inference')
    if not isinstance(candidates, (list, tuple)) or not 1 <= len(candidates) <= 134:
        raise ValueError('complete supported pool of 1..134 candidates required')
    chunks = [candidates[start:start + 9] for start in range(0, len(candidates), 9)]
    for chunk in chunks:
        TinySequenceScorer._validate(prefix, chunk)
    outputs = []
    with torch.no_grad():
        for chunk in chunks:
            result = model(prefix, chunk)
            if (result.shape != (len(chunk),) or result.dtype != torch.float32
                    or result.device.type != 'cpu' or not torch.isfinite(result).all()):
                raise ValueError('invalid chunk output')
            outputs.append(result)
        complete = torch.cat(outputs, dim=0)
    if complete.shape != (len(candidates),) or complete.requires_grad:
        raise RuntimeError('full-pool inference contract violated')
    return complete
