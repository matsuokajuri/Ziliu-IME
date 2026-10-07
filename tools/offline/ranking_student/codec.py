"""Deterministic Unicode-character vocabulary with lossless UTF-8 fallback."""
from .config import Config
from .contracts import digest, text, validate_request
from types import MappingProxyType

PAD, CLS, SEP, RESERVED = 0, 1, 2, 3
BYTE_BASE, CHAR_BASE = 4, 260


class Codec:
    def __init__(self, characters=(), config=Config()):
        self.config = config
        chars = tuple(characters)
        if len(chars) > config.vocab_size-CHAR_BASE or len(set(chars)) != len(chars):
            raise ValueError("bounded unique frozen character vocabulary required")
        for c in chars:
            text(c, 1)
        self.index = MappingProxyType({c: CHAR_BASE+i for i, c in enumerate(chars)})
        self.inverse = MappingProxyType(dict(enumerate(chars, CHAR_BASE)))
        self.sha256 = digest({"schema": "char-utf8-fallback.v1", "characters": chars,
                              "vocab_size": config.vocab_size})

    def encode(self, value):
        value.encode("utf-8", errors="strict")
        result = []
        for c in value:
            if c in self.index:
                result.append(self.index[c])
            else:
                result.extend(BYTE_BASE+b for b in c.encode("utf-8"))
        return result

    def decode(self, ids):
        output, pending = [], bytearray()
        def flush():
            if pending:
                output.append(pending.decode("utf-8", errors="strict"))
                pending.clear()
        for token in ids:
            if type(token) is not int:
                raise ValueError("integer token required")
            if BYTE_BASE <= token < CHAR_BASE:
                pending.append(token-BYTE_BASE)
            elif token in self.inverse:
                flush()
                output.append(self.inverse[token])
            else:
                raise ValueError("unknown/special token in text decode")
        flush()
        return "".join(output)

    def plan(self, requests):
        c = self.config
        if type(requests) is not list or not 1 <= len(requests) <= c.max_batch:
            raise ValueError("bounded explicit batch required")
        encoded = []
        for request in requests:
            ids = validate_request(request, c)
            prefix, pinyin = self.encode(request["prefix"]), self.encode(request["pinyin"])
            source = [CLS] + prefix + [SEP] + pinyin + [SEP]
            segments = [0]*(len(prefix)+2) + [1]*(len(pinyin)+1)
            candidates = [[CLS]+self.encode(x["text"]) for x in request["candidates"]]
            if len(source) > c.source_tokens or any(len(x) > c.candidate_tokens for x in candidates):
                raise ValueError("complete encoding exceeds token budget; no candidate truncation")
            encoded.append((source, segments, candidates, ids))
        s = max(len(x[0]) for x in encoded)
        k = max(len(x[2]) for x in encoded)
        t = max(len(y) for x in encoded for y in x[2])
        result = {name: [] for name in ("source_ids", "source_mask", "source_segments",
                                       "candidate_ids", "candidate_mask", "pool_mask", "source_indices")}
        for source, segments, candidates, ids in encoded:
            result["source_ids"].append(source+[PAD]*(s-len(source)))
            result["source_mask"].append([True]*len(source)+[False]*(s-len(source)))
            result["source_segments"].append(segments+[0]*(s-len(source)))
            result["candidate_ids"].append([x+[PAD]*(t-len(x)) for x in candidates]+[[CLS]+[PAD]*(t-1)]*(k-len(ids)))
            # Dummy padded candidates have a visible CLS, avoiding all-masked softmax.
            result["candidate_mask"].append([[True]*len(x)+[False]*(t-len(x)) for x in candidates]+[[True]+[False]*(t-1)]*(k-len(ids)))
            result["pool_mask"].append([True]*len(ids)+[False]*(k-len(ids)))
            result["source_indices"].append(ids+[-1]*(k-len(ids)))
        result["request_sha256"] = [digest(request) for request in requests]
        result["codec_sha256"] = self.sha256
        return result
