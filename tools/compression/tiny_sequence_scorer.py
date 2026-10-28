"""Research candidate-score student; no bundled weights, data, or downloads.

A caller supplies a verified frozen embedding. Scores are not language-model
log probabilities. This module does not implement input-method adoption policy.
"""
import torch
from torch import nn
from torch.nn import functional as F


class TinySequenceScorer(nn.Module):
    """23,041 trainable parameters plus an external frozen 8192x192 table."""
    def __init__(self, embedding, seed=20261028):
        super().__init__()
        if (embedding.shape != (8192, 192) or embedding.dtype != torch.float32
                or embedding.device.type != 'cpu' or not torch.isfinite(embedding).all()):
            raise ValueError('finite CPU float32 embedding with shape (8192,192) required')
        self.register_buffer('embedding', embedding.detach().clone(), persistent=False)
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(seed)
            self.projection = nn.Linear(192, 32, bias=False, device='cpu')
            self.context = nn.GRU(32, 32, batch_first=True, device='cpu')
            self.candidate = nn.GRU(32, 32, batch_first=True, device='cpu')
            self.hidden = nn.Linear(130, 32, device='cpu')
            self.output = nn.Linear(32, 1, device='cpu')
        if sum(p.numel() for p in self.parameters()) != 23041:
            raise AssertionError('student geometry changed')

    @staticmethod
    def _validate(prefix, candidates):
        if (not isinstance(prefix, (list, tuple)) or not prefix or prefix[0] != 2
                or any(type(i) is not int or not 0 <= i < 8192 for i in prefix)):
            raise ValueError('exact bounded BOS/context prefix required')
        if not isinstance(candidates, (list, tuple)) or not 1 <= len(candidates) <= 9:
            raise ValueError('one to nine candidates required')
        for tail in candidates:
            if (not isinstance(tail, (list, tuple)) or not tail
                    or len(prefix) + len(tail) > 64
                    or any(type(i) is not int or not 3 <= i < 8192 for i in tail)):
                raise ValueError('candidate and context exceed vocabulary/position contract')

    def forward(self, prefix, candidates):
        self._validate(prefix, candidates)
        context = self.projection(self.embedding[list(prefix)]).unsqueeze(0)
        _, hidden = self.context(context)
        h = hidden[0, 0]
        features = []
        for tail in candidates:
            projected = self.projection(self.embedding[list(tail)]).unsqueeze(0)
            sequence, final = self.candidate(projected)
            last = final[0, 0]
            mean = sequence[0].mean(dim=0)
            lengths = h.new_tensor([len(tail) / 63, len(prefix) / 64])
            features.append(torch.cat([h, last, mean, h * last, lengths]))
        # Per-candidate MLP keeps FP32 operation shape independent of pool order.
        result = torch.stack([self.output(torch.tanh(self.hidden(row))).squeeze(0)
                              for row in features])
        if not torch.isfinite(result).all():
            raise ValueError('nonfinite student scores')
        return result


def compression_loss(student_scores, teacher_scores):
    """Per-condition KL at T=1 plus 0.1 centered-score MSE; teacher detached."""
    if (student_scores.ndim != 1 or teacher_scores.shape != student_scores.shape
            or not 1 <= student_scores.numel() <= 9
            or not torch.isfinite(student_scores).all()
            or not torch.isfinite(teacher_scores).all()):
        raise ValueError('matching finite bounded score vectors required')
    target = teacher_scores.detach().to(student_scores)
    kl = F.kl_div(F.log_softmax(student_scores, dim=0),
                  F.softmax(target, dim=0), reduction='sum')
    difference = ((student_scores - student_scores.mean())
                  - (target - target.mean()))
    return kl + 0.1 * difference.square().mean()
