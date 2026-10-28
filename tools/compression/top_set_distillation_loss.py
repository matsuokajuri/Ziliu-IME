"""Experimental per-list ranking auxiliary loss; no acceptance or quality claim.

Inspired by pairwise logistic ranking (RankNet) and top-ranking distillation:
https://www.microsoft.com/en-us/research/wp-content/uploads/2005/08/icml_ranking.pdf
https://proceedings.mlr.press/v130/reddi21a.html
This is not a reproduction of RankDistil. No candidate position is a feature.
Training loss for lists of 1..9 candidates only; not a DEV scoring/evaluation
API and not applicable to full candidate pools with more than nine entries.
"""
import torch
import torch.nn.functional as F
from tiny_sequence_scorer import compression_loss

TOP_SET_EPSILON = 1e-6
AUXILIARY_WEIGHT = 0.1


def top_set_pairwise_loss(student_scores, teacher_scores):
    """Mean softplus(s_rest - s_top) across all top-set/rest pairs.

    Top set: max(teacher) - teacher <= 1e-6, computed in the teacher's
    original floating dtype; no downcast changes membership. Teacher is
    detached. All-tied/single-item lists return differentiable zero.
    Caller retains responsibility for equal per-condition/per-scene weighting.
    """
    if (student_scores.ndim != 1 or teacher_scores.shape != student_scores.shape
            or not 1 <= student_scores.numel() <= 9
            or not student_scores.is_floating_point()
            or not teacher_scores.is_floating_point()
            or not torch.isfinite(student_scores).all()
            or not torch.isfinite(teacher_scores).all()):
        raise ValueError('matching finite floating score vectors of length 1..9 required')
    target = teacher_scores.detach().to(device=student_scores.device)
    top = target.max() - target <= TOP_SET_EPSILON
    if bool(top.all()):
        return student_scores[0] * 0.0
    differences = student_scores[~top][None, :] - student_scores[top][:, None]
    if not torch.isfinite(differences).all():
        raise ValueError('pairwise score difference overflow')
    loss = F.softplus(differences).mean()
    if not torch.isfinite(loss):
        raise ValueError('pairwise loss overflow')
    return loss


def comparison_loss(student_scores, teacher_scores, *, arm):
    """A is the unchanged legacy loss; B adds exactly 0.1 ranking auxiliary.

    This function does not select an arm or tune any coefficient from data.
    """
    if arm not in ('A', 'B'):
        raise ValueError('arm must be A or B')
    baseline = compression_loss(student_scores, teacher_scores)
    if not torch.isfinite(baseline):
        raise ValueError('baseline loss overflow')
    result = baseline if arm == 'A' else baseline + AUXILIARY_WEIGHT * top_set_pairwise_loss(student_scores, teacher_scores)
    if not torch.isfinite(result):
        raise ValueError('combined loss overflow')
    return result
