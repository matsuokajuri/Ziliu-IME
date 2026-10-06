# SPDX-License-Identifier: GPL-3.0-only
"""Small synthetic float32 GPT semantics control; contains no Cassotis weights.

Tests tree masks, positions, token alignment and prefix KV reuse mathematically.
Optional input quantize/dequantize shows a possible batch-range mechanism; it
does not reproduce ORT QAttention's integer kernel or prove the real root cause.
"""
import numpy as np
from cassotis_onnx_adapter import pack


def normalize(x):
    return (x - x.mean(-1, keepdims=True)) / np.sqrt(x.var(-1, keepdims=True) + np.float32(1e-5))


def dynamic_qdq(x):
    """ONNX DynamicQuantizeLinear-11 scalar/per-tensor range, followed by dequant."""
    low, high = min(0., float(x.min())), max(0., float(x.max()))
    scale = np.float32((high - low) / 255) if high != low else np.float32(1)
    zero = np.clip(np.rint(np.float32(-low) / scale), 0, 255).astype(np.uint8)
    quantized = np.clip(np.rint(x / scale) + zero, 0, 255).astype(np.uint8)
    return (quantized.astype(np.float32) - zero) * scale


class FloatGPT:
    def __init__(self, *, dynamic=False):
        rng = np.random.default_rng(20261006)
        self.dynamic = dynamic
        self.embeddings = rng.normal(0, .2, (32, 16)).astype(np.float32)
        self.positions = rng.normal(0, .1, (256, 16)).astype(np.float32)
        self.layers = [tuple(rng.normal(0, .12, shape).astype(np.float32)
                            for shape in ((16, 48), (16, 16), (16, 64), (64, 16))) for _ in range(2)]

    def linear(self, x, weight):
        return (dynamic_qdq(x) if self.dynamic else x) @ weight

    def run(self, tree, past=None):
        x = self.embeddings[tree.ids] + self.positions[tree.positions]
        past_length = 0 if past is None else past[0].shape[3]
        mask = tree.mask(past_length)[0].astype(bool)
        presents = []
        for index, (qkv, project, up, down) in enumerate(self.layers):
            q, k, v = np.split(self.linear(normalize(x), qkv), 3, axis=-1)
            q, k, v = [a.reshape(-1, 4, 4).transpose(1, 0, 2) for a in (q, k, v)]
            if past is not None:
                k = np.concatenate([past[index][0, 0], k], axis=1)
                v = np.concatenate([past[index][1, 0], v], axis=1)
            presents.append(np.stack([k, v])[:, None])
            logits = (q @ k.transpose(0, 2, 1)) / np.float32(2)
            logits = np.where(mask[None], logits, np.float32(-1e9))
            attention = np.exp(logits - logits.max(-1, keepdims=True))
            attention /= attention.sum(-1, keepdims=True)
            output = (attention @ v).transpose(1, 0, 2).reshape(-1, 16)
            x = x + self.linear(output, project)
            hidden = self.linear(normalize(x), up)
            gelu = np.float32(.5) * hidden * (1 + np.tanh(np.sqrt(np.float32(2 / np.pi)) *
                                                       (hidden + np.float32(.044715) * hidden**3)))
            x = x + self.linear(gelu, down)
        logits = self.linear(normalize(x), self.embeddings.T)
        shifted = logits - logits.max(-1, keepdims=True)
        return shifted - np.log(np.exp(shifted).sum(-1, keepdims=True)), presents

    def scores(self, prefix, candidates, mode):
        if mode == "independent":
            return [self.scores(prefix, [tokens], "packed")[0] for tokens in candidates]
        if mode == "packed":
            tree = pack(prefix, candidates)
            logp, _ = self.run(tree)
            return [sum(float(logp[tree.parents[node], tree.ids[node]]) for node in path) / len(path)
                    for path in tree.paths]
        root, past = self.run(pack(prefix, []))
        tree = pack([], candidates, cached_length=len(prefix))
        logp, _ = self.run(tree, past)
        return [sum(float(root[-1, tree.ids[node]]) if tree.parents[node] == -1 else
                    float(logp[tree.parents[node], tree.ids[node]]) for node in path) / len(path)
                for path in tree.paths]


def controls():
    fixtures = [([0], [[2], [2, 3], [2, 4], [5, 6, 7]]),
                ([0, 2, 3], [[4, 5, 6], [4, 5, 7], [8, 9], [4], [10, 11, 12, 13]]),
                ([0, *([2, 3, 4] * 20)], [[5], [5, 6], [7, 8, 9]])]
    result = {"seed": 20261006, "source": "first-party synthetic 2-layer/16-hidden/4-head float32 GPT",
              "real_model_loaded": False, "real_kernel_root_cause_proved": False, "fixtures": []}
    for prefix, candidates in fixtures:
        row = {"prefix_tokens": len(prefix), "candidate_tokens": list(map(len, candidates))}
        for dynamic in (False, True):
            model = FloatGPT(dynamic=dynamic)
            packed = np.asarray(model.scores(prefix, candidates, "packed"))
            independent = np.asarray(model.scores(prefix, candidates, "independent"))
            cached = np.asarray(model.scores(prefix, candidates, "cached"))
            row["dynamic_activation_qdq" if dynamic else "float32"] = {
                "packed_independent_max_abs_mean_error": float(np.max(np.abs(packed - independent))),
                "packed_cached_max_abs_mean_error": float(np.max(np.abs(packed - cached))),
                "packed_top1": int(packed.argmax()), "independent_top1": int(independent.argmax()),
                "cached_top1": int(cached.argmax())}
        result["fixtures"].append(row)
    return result
