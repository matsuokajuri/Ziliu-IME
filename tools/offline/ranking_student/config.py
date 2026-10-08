"""Explicit geometry and exact parameter accounting; standard library only."""
from dataclasses import asdict, dataclass


@dataclass(frozen=True)
class Config:
    vocab_size: int = 8192
    width: int = 256
    heads: int = 4
    feedforward: int = 1024
    context_layers: int = 7
    candidate_layers: int = 2
    head_width: int = 512
    max_prefix_scalars: int = 48
    max_pinyin_scalars: int = 64
    max_candidate_scalars: int = 63
    max_candidates: int = 9
    max_batch: int = 8

    def __post_init__(self):
        if any(type(v) is not int or v <= 0 for v in asdict(self).values()):
            raise ValueError("positive integer geometry required")
        if self.width % self.heads or self.vocab_size < 260:
            raise ValueError("heads must divide width; byte fallback needs 260 IDs")
        if (self.width > 256 or self.feedforward > 1024 or self.head_width > 512
                or self.context_layers > 7 or self.candidate_layers > 2
                or self.vocab_size > 8192 or self.max_batch > 8
                or self.max_prefix_scalars > 48 or self.max_pinyin_scalars > 64
                or self.max_candidate_scalars > 63 or self.max_candidates > 32):
            raise ValueError("geometry exceeds this bounded prototype")

    @property
    def source_tokens(self):
        # CLS + <=4 UTF-8 bytes/scalar + SEP + ASCII pinyin + SEP.
        return 3 + 4 * self.max_prefix_scalars + self.max_pinyin_scalars

    @property
    def candidate_tokens(self):
        return 1 + 4 * self.max_candidate_scalars

    def parameter_breakdown(self):
        d, f = self.width, self.feedforward
        block = 4*d*d + 2*d*f + 9*d + f
        return {
            "shared_token_embedding": self.vocab_size*d,
            "source_position_embedding": self.source_tokens*d,
            "candidate_position_embedding": self.candidate_tokens*d,
            "segment_embedding": 3*d,
            "context_blocks": self.context_layers*block,
            "candidate_blocks": self.candidate_layers*block,
            "final_encoder_norms": 4*d,
            "shared_cross_qkv_out": 4*d*d + 4*d,
            "cross_norm": 2*d,
            "score_head": (4*d+1)*self.head_width + self.head_width+1,
        }

    def parameter_count(self):
        return sum(self.parameter_breakdown().values())


def tiny_config():
    """For future explicitly authorized synthetic tensor tests only."""
    return Config(vocab_size=512, width=16, heads=4, feedforward=32,
                  context_layers=1, candidate_layers=1, head_width=16)
