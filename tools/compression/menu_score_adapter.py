"""Float64 score translation for a legacy nonpositive menu-score boundary.

These are translated ranking scores, NOT probabilities or mean log likelihoods.
Keep original scores separately for metrics and audit; do not overwrite them.
This helper performs no model inference and consumes no teacher scores.
"""
import math
import numbers


def _same_pair_order(before, after):
    for i in range(len(before)):
        for j in range(i):
            if ((before[i] > before[j]) != (after[i] > after[j])
                    or (before[i] < before[j]) != (after[i] < after[j])
                    or (before[i] == before[j]) != (after[i] == after[j])):
                return False
    return True


def shift_scores_for_menu(raw_scores):
    """Copy 1..134 scores to Python float64 and subtract their shared maximum.

    Input order and length are preserved. All strict pairwise order relations
    and ties must survive both conversion and translation. Overflow, loss of
    distinguishability, or any non-finite value causes rejection, never clipping
    or a silently changed ranking. No tolerance-based tie merging is used.
    The caller must retain raw_scores separately; this function does not mutate
    them. Use only at the menu boundary, not as a calibrated probability output.
    """
    original = list(raw_scores)
    if not 1 <= len(original) <= 134:
        raise ValueError('a complete list of 1..134 scores is required')
    if any(isinstance(x, bool) or not isinstance(x, numbers.Real) for x in original):
        raise ValueError('real numeric scores, excluding booleans, are required')
    try:
        scores = [float(x) for x in original]
    except (OverflowError, ValueError) as error:
        raise ValueError('score cannot be represented as finite float64') from error
    if not all(math.isfinite(x) for x in scores):
        raise ValueError('non-finite input score')
    if not _same_pair_order(original, scores):
        raise ValueError('float64 conversion changes ordering or ties')
    maximum = max(scores)
    shifted = [x - maximum for x in scores]
    if not all(math.isfinite(x) and x <= 0.0 for x in shifted):
        raise ValueError('non-finite or positive translated score')
    if max(shifted) != 0.0 or not _same_pair_order(scores, shifted):
        raise ValueError('translation changes ordering or ties')
    return shifted
