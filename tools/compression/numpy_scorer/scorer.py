"""Independent fixed R59 NumPy/OpenBLAS scorer, NOT original Torch A.
Original NumPy implementation of a fixed character-transformer architecture.
Architecture reference: chinese-ime-lm reference/ (Apache-2.0); see NOTICE.md.
This does not relicense the upstream repository root (GPL-3.0).
Import uses stdlib only; NumPy is explicitly injected after external admission.
"""
import hashlib
import math

SCHEMA='R59_NUMPY_OPENBLAS_FP32_ERF_V1'

def need(x,message):
    if not x:raise ValueError(message)

def geometry():
    out={'tok.weight':(8192,192),'pos.weight':(64,192),'ln_f.weight':(192,),'ln_f.bias':(192,)}
    for i in range(6):
        p=f'blocks.{i}.'
        for norm in ('ln1','ln2'):
            for suffix in ('weight','bias'):out[p+norm+'.'+suffix]=(192,)
        for name,n,k in [('qkv',576,192),('proj',192,192),('fc',768,192),('out',192,768)]:
            out[p+name+'.weight']=(n,k);out[p+name+'.bias']=(n,)
    return out

def ids_ok(ids,*,minimum=1,maximum=64,supported=False):
    need(type(ids) in (list,tuple) and minimum<=len(ids)<=maximum,'bounded explicit token list')
    low=3 if supported else 0
    need(all(type(i) is int and low<=i<8192 for i in ids),'bounded integer token IDs, no bool or unsupported tail')

def candidate_input(prefix,tail):
    ids_ok(prefix);ids_ok(tail,maximum=63,supported=True)
    need(prefix[0]==2 and len(prefix)+len(tail)<=64,'BOS prefix and complete shared-context bound')
    return list(prefix)+list(tail[:-1])

class Operations:
    def __init__(self,np):self.np=np
    def checked(self,a):
        n=self.np
        need(type(a) is n.ndarray and a.dtype==n.dtype('float32') and bool(n.isfinite(a).all()),'finite exact FP32 array')
        return a
    def linear(self,x,w,b):
        n=self.np
        # BLAS accumulation is implementation-specific; result rounded to FP32,
        # then separate FP32 bias addition (no claim of Torch fusion equivalence).
        return self.checked(n.add(n.matmul(x,w.T),b,dtype=n.float32))
    def layer_norm(self,x,w,b):
        n=self.np
        mean=n.mean(x,axis=-1,keepdims=True,dtype=n.float32)
        centered=n.subtract(x,mean,dtype=n.float32)
        variance=n.mean(n.multiply(centered,centered,dtype=n.float32),axis=-1,keepdims=True,dtype=n.float32)
        denominator=n.sqrt(n.add(variance,n.float32(1e-5),dtype=n.float32),dtype=n.float32)
        normalized=n.divide(centered,denominator,dtype=n.float32)
        return self.checked(n.add(n.multiply(normalized,w,dtype=n.float32),b,dtype=n.float32))
    def gelu(self,x):
        n=self.np;self.checked(x)
        # Explicit new numerical contract: x/sqrt2 in binary64, stdlib/libm erf
        # binary64 -> FP32; then (x*0.5)_f32 * (1+erf)_f32 -> FP32.
        erf=n.fromiter((math.erf(float(v)/math.sqrt(2.0)) for v in x.flat),dtype=n.float32,count=x.size).reshape(x.shape)
        return self.checked(n.multiply(n.multiply(x,n.float32(0.5),dtype=n.float32),
                            n.add(n.float32(1),erf,dtype=n.float32),dtype=n.float32))
    def softmax(self,logits):
        n=self.np
        # Input may contain causal -inf, but each row must have finite maximum.
        maximum=n.max(logits,axis=-1,keepdims=True)
        need(bool(n.isfinite(maximum).all()),'nonempty finite softmax row')
        shifted=n.subtract(logits,maximum,dtype=n.float32)
        exps=n.exp(shifted,dtype=n.float32)
        denom=n.sum(exps,axis=-1,keepdims=True,dtype=n.float32)
        return self.checked(n.divide(exps,denom,dtype=n.float32))
    def log_softmax(self,logits):
        n=self.np;self.checked(logits)
        shifted=n.subtract(logits,n.max(logits,axis=-1,keepdims=True),dtype=n.float32)
        logsum=n.log(n.sum(n.exp(shifted,dtype=n.float32),axis=-1,keepdims=True,dtype=n.float32),dtype=n.float32)
        return self.checked(n.subtract(shifted,logsum,dtype=n.float32))

class Scorer:
    def __init__(self,np,weights):
        self.np=np;self.ops=Operations(np)
        need(type(weights) is dict and set(weights)==set(geometry()),'exact parameter names')
        for name,shape in geometry().items():
            a=self.ops.checked(weights[name]);need(tuple(a.shape)==shape and a.flags.c_contiguous and not a.flags.writeable,'readonly contiguous endpoint geometry')
        self.weights=dict(weights)
        self._weight_identity={name:(id(a),hashlib.sha256(a.tobytes(order='C')).hexdigest()) for name,a in self.weights.items()}
    def assert_weights(self):
        need(set(self.weights)==set(geometry()),'weight key identity unchanged')
        for name,shape in geometry().items():
            a=self.ops.checked(self.weights[name])
            need(tuple(a.shape)==shape and a.flags.c_contiguous and not a.flags.writeable,'weight geometry/readonly flags unchanged')
            need((id(a),hashlib.sha256(a.tobytes(order='C')).hexdigest())==self._weight_identity[name],'full weight bytes and object identity unchanged')
    def forward(self,ids):
        ids_ok(ids);ids=list(ids);n=self.np;o=self.ops;w=self.weights;t=len(ids)
        with n.errstate(over='raise',divide='raise',invalid='raise',under='ignore'):
            x=o.checked(n.add(w['tok.weight'][ids],w['pos.weight'][:t],dtype=n.float32))
            allowed=n.arange(t)[None,:]<=n.arange(t)[:,None]
            for i in range(6):
                p=f'blocks.{i}.'
                y=o.layer_norm(x,w[p+'ln1.weight'],w[p+'ln1.bias'])
                qkv=o.linear(y,w[p+'qkv.weight'],w[p+'qkv.bias'])
                q,k,v=[part.reshape(t,4,48).transpose(1,0,2) for part in n.split(qkv,3,axis=-1)]
                attention=n.divide(n.matmul(q,k.transpose(0,2,1)),n.float32(math.sqrt(48)),dtype=n.float32)
                attention=n.where(allowed,attention,n.float32(-n.inf))
                attended=o.checked(n.matmul(o.softmax(attention),v).transpose(1,0,2).reshape(t,192))
                x=o.checked(n.add(x,o.linear(attended,w[p+'proj.weight'],w[p+'proj.bias']),dtype=n.float32))
                y=o.layer_norm(x,w[p+'ln2.weight'],w[p+'ln2.bias'])
                hidden=o.gelu(o.linear(y,w[p+'fc.weight'],w[p+'fc.bias']))
                x=o.checked(n.add(x,o.linear(hidden,w[p+'out.weight'],w[p+'out.bias']),dtype=n.float32))
            normalized=o.layer_norm(x,w['ln_f.weight'],w['ln_f.bias'])
            return o.checked(n.matmul(normalized,w['tok.weight'].T))
    def score(self,prefix,tail):
        ids=candidate_input(prefix,tail);n=self.np;self.assert_weights()
        logits=self.forward(ids)
        # Original A normalizes all8192 vocabulary logits only at suffix target
        # positions, after full-sequence forward. Do not normalize unused prefix.
        logps=self.ops.log_softmax(logits[len(prefix)-1:])
        need(logps.shape==(len(tail),8192),'exact suffix prediction alignment')
        values=logps[n.arange(len(tail)),list(tail)]
        result=n.mean(values,dtype=n.float32)
        need(bool(n.isfinite(result)) and float(result)<=0,'finite nonpositive candidate log-probability')
        self.assert_weights()
        return float(result)
