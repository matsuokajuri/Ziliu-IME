"""Lazy PyTorch implementation. No heavy import, allocation, checkpoint or download on import."""
from .config import Config
from .contracts import hash_value, validate_training_row


def build_model(config=Config(), *, runtime_authorized=False):
    """Caller must already own an authorized resource window/Job; flag is not a lease."""
    if runtime_authorized is not True:
        raise PermissionError("model allocation requires a separately authorized runtime window")
    import torch
    from torch import nn
    from torch.nn import functional as F

    d, h = config.width, config.heads

    class Block(nn.Module):
        def __init__(self):
            super().__init__()
            self.norm1 = nn.LayerNorm(d)
            self.qkv = nn.Linear(d, 3*d)
            self.out = nn.Linear(d, d)
            self.norm2 = nn.LayerNorm(d)
            self.ff1 = nn.Linear(d, config.feedforward)
            self.ff2 = nn.Linear(config.feedforward, d)

        def forward(self, x, mask):
            b, n, _ = x.shape
            q, k, v = [z.reshape(b,n,h,d//h).transpose(1,2)
                       for z in self.qkv(self.norm1(x)).chunk(3, dim=-1)]
            weights = (q @ k.transpose(-1,-2))/(d//h)**0.5
            weights = weights.masked_fill(~mask[:,None,None,:], -torch.inf).softmax(-1)
            attended = (weights @ v).transpose(1,2).reshape(b,n,d)
            x = x + self.out(attended)
            x = x + self.ff2(F.gelu(self.ff1(self.norm2(x))))
            return x.masked_fill(~mask[:,:,None], 0)

    class Ranker(nn.Module):
        def __init__(self):
            super().__init__()
            self.config = config
            self.codec_sha256 = None
            self.token = nn.Embedding(config.vocab_size, d, padding_idx=0)
            self.source_position = nn.Embedding(config.source_tokens, d)
            self.candidate_position = nn.Embedding(config.candidate_tokens, d)
            self.segment = nn.Embedding(3, d)
            self.context_blocks = nn.ModuleList(Block() for _ in range(config.context_layers))
            self.candidate_blocks = nn.ModuleList(Block() for _ in range(config.candidate_layers))
            self.context_norm = nn.LayerNorm(d)
            self.candidate_norm = nn.LayerNorm(d)
            self.cross_q = nn.Linear(d, d)
            self.cross_k = nn.Linear(d, d)
            self.cross_v = nn.Linear(d, d)
            self.cross_out = nn.Linear(d, d)
            self.cross_norm = nn.LayerNorm(d)
            self.score_head = nn.Sequential(nn.Linear(4*d, config.head_width), nn.GELU(),
                                            nn.Linear(config.head_width, 1))

        def bind_codec(self, sha256):
            hash_value(sha256)
            if self.codec_sha256 is not None and self.codec_sha256 != sha256:
                raise ValueError("model already bound to a different frozen tokenizer")
            self.codec_sha256 = sha256

        def tensors(self, plan):
            self.bind_codec(plan["codec_sha256"])
            device = self.token.weight.device
            return {key: torch.tensor(value, dtype=torch.bool if key.endswith("mask") else torch.long,
                                      device=device)
                    for key, value in plan.items() if key in {"source_ids","source_mask","source_segments",
                                                             "candidate_ids","candidate_mask","pool_mask"}}

        def forward(self, source_ids, source_mask, source_segments, candidate_ids, candidate_mask, pool_mask):
            b, s = source_ids.shape
            bc, k, t = candidate_ids.shape
            if (b != bc or not 1 <= b <= config.max_batch or not 1 <= k <= config.max_candidates
                    or not 1 <= s <= config.source_tokens or not 1 <= t <= config.candidate_tokens
                    or source_mask.shape != source_ids.shape or source_segments.shape != source_ids.shape
                    or candidate_mask.shape != candidate_ids.shape or pool_mask.shape != (b,k)):
                raise ValueError("tensor geometry outside bounded plan")
            if (any(x.dtype != torch.bool for x in (source_mask,candidate_mask,pool_mask))
                    or not source_mask.any(-1).all() or not candidate_mask.any(-1).all()
                    or not pool_mask.any(-1).all()):
                raise ValueError("explicit masks with a valid sentinel and nonempty pools required")
            # IDs/segments are produced only by the strict stdlib Codec plan.
            x = self.token(source_ids) + self.source_position(torch.arange(s,device=source_ids.device))[None]
            x = x + self.segment(source_segments)
            for block in self.context_blocks:
                x = block(x,source_mask)  # B,S,D: context encoded exactly once per request.
            x = self.context_norm(x)
            candidate_flat = candidate_ids.reshape(b*k,t)
            mask_flat = candidate_mask.reshape(b*k,t)
            y = self.token(candidate_flat) + self.candidate_position(torch.arange(t,device=source_ids.device))[None]
            y = y + self.segment.weight[2]
            for block in self.candidate_blocks:
                y = block(y,mask_flat)
            y = self.candidate_norm(y)
            pooled = (y*mask_flat[:,:,None]).sum(1)/mask_flat.sum(1)[:,None]
            pooled = pooled.reshape(b,k,d)
            q = self.cross_q(pooled).reshape(b,k,h,d//h).transpose(1,2)
            # Shared K/V stay B,H,S,Dh; no repeat over K and no decoder KV cache.
            key = self.cross_k(x).reshape(b,s,h,d//h).transpose(1,2)
            value = self.cross_v(x).reshape(b,s,h,d//h).transpose(1,2)
            weights = (q @ key.transpose(-1,-2))/(d//h)**0.5
            weights = weights.masked_fill(~source_mask[:,None,None,:],-torch.inf).softmax(-1)
            attended = (weights @ value).transpose(1,2).reshape(b,k,d)
            z = self.cross_norm(pooled+self.cross_out(attended))
            context = (x*source_mask[:,:,None]).sum(1)/source_mask.sum(1)[:,None]
            context = context[:,None,:].expand(-1,k,-1)
            features = torch.cat((z,context,z*context,z-context),dim=-1)
            return self.score_head(features).squeeze(-1).masked_fill(~pool_mask,-torch.inf)

        def objective(self, scores, plan, rows):
            if len(rows) != scores.shape[0]:
                raise ValueError("one training row per encoded request required")
            self.bind_codec(plan["codec_sha256"])
            losses = []
            for b, row in enumerate(rows):
                target = validate_training_row(row, config)
                if plan["request_sha256"][b] != row["pool_sha256"]:
                    raise ValueError("loss target and complete encoded request differ")
                if target is None:
                    continue
                ids = target["candidate_source_indices"]
                if plan["source_indices"][b][:len(ids)] != ids:
                    raise ValueError("loss and encoded pool identities differ")
                s = scores[b,:len(ids)]
                if not torch.isfinite(s).all():
                    raise ValueError("nonfinite valid logits")
                if target["kind"] == "teacher_distribution":
                    p = s.new_tensor(target["probabilities"])
                    listwise = -(p*F.log_softmax(s,dim=-1)).sum()
                    raw = target["probabilities"]
                    # Compute ratios before narrowing tiny nonzero values to float32.
                    pairs = [(i,j,raw[i]/(raw[i]+raw[j])) for i in range(len(ids)) for j in range(i+1,len(ids))
                             if raw[i]+raw[j] > 0]
                else:
                    positive = [i for i, ident in enumerate(ids) if ident in target["acceptable_source_indices"]]
                    listwise = torch.logsumexp(s,0)-torch.logsumexp(s[positive],0)
                    pairs = [(i,j,1.0) for i in positive for j in range(len(ids)) if j not in positive]
                pairwise = torch.stack([F.softplus(s[i]-s[j])-q*(s[i]-s[j])
                                        for i,j,q in pairs]).mean() if pairs else s.sum()*0
                losses.append(listwise+0.2*pairwise)
            if not losses:
                raise ValueError("no resolved ranking supervision; no optimizer step permitted")
            return torch.stack(losses).mean()

        def backward_checked(self, loss):
            if loss.ndim != 0 or not torch.isfinite(loss):
                raise ValueError("nonfinite/non-scalar loss; optimizer update refused")
            loss.backward()

        def finite_gradient_norm(self):
            for parameter in self.parameters():
                if parameter.grad is not None and not torch.isfinite(parameter.grad).all():
                    raise ValueError("nonfinite gradient; optimizer update refused")
            return nn.utils.clip_grad_norm_(self.parameters(),1.0,error_if_nonfinite=True)

    model = Ranker()
    if sum(p.numel() for p in model.parameters()) != config.parameter_count():
        raise AssertionError("module parameters differ from stdlib accounting")
    return model


def train_step(model, optimizer, rows, codec, *, runtime_authorized=False):
    """One bounded update primitive, not a launcher or authority to read/train data."""
    if runtime_authorized is not True:
        raise PermissionError("training requires its own authorized resource window")
    if model.config != codec.config:
        raise ValueError("model/tokenizer geometry mismatch")
    model.bind_codec(codec.sha256)
    if not any(validate_training_row(row,codec.config) is not None for row in rows):
        raise ValueError("no resolved supervision")
    # Validate all rows even if the first one was resolved.
    for row in rows:
        validate_training_row(row,codec.config)
    plan = codec.plan([r["request"] for r in rows])
    model.train()
    optimizer.zero_grad(set_to_none=True)
    scores = model(**model.tensors(plan))
    loss = model.objective(scores,plan,rows)
    model.backward_checked(loss)
    norm = model.finite_gradient_norm()
    optimizer.step()
    return {"loss":float(loss.detach()), "gradient_norm":float(norm), "rows":len(rows)}
