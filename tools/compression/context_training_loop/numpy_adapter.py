"""Injected model + AdamW adapter; importing never imports NumPy or executes math.
Source-only, requires separately admitted numeric launcher and provenance loader.
"""
import math
import random
import struct
from training_loop import need
from checkpoint_store import digest


def tuples(v): return tuple(tuples(x) for x in v) if isinstance(v,list) else v

def validate_records(arrays, shapes, nonnegative=False):
    need(type(arrays) is dict and set(arrays)==set(shapes),'exact parameter/moment keys')
    for k,shape in shapes.items():
        r=arrays[k]
        need(set(r)=={'shape','dtype','hex'} and r['shape']==shape and r['dtype']=='<f8','array metadata')
        need(type(r['hex']) is str,'hex string')
        raw=bytes.fromhex(r['hex']);count=math.prod(shape)
        need(len(raw)==count*8 and raw.hex()==r['hex'],'canonical exact float64 bytes')
        for (v,) in struct.iter_unpack('<d',raw):
            need(math.isfinite(v) and (not nonnegative or v>=0),'finite array/second moment')

class NumpyBackend:
    def __init__(self,np,model,gru,objective,adamw):
        self.np=np;self.model=model;self.gru=gru;self.objective=objective;self.adamw=adamw
    def input_digest(self,record):
        # Serialization only; no model, native library getter, or numerical kernels.
        def packed(a):
            need(type(a) is self.np.ndarray and str(a.dtype)=='float64' and a.ndim==2,'embedding matrix serialization')
            return {'shape':list(a.shape),'dtype':a.dtype.str,'hex':a.tobytes(order='C').hex()}
        value={**record,'context':packed(record['context']),'candidates':[packed(a) for a in record['candidates']]}
        return digest(value)
    def validate_state(self,state,identity):
        need(identity['optimizer_config']==self.adamw.CONFIG,'unchanged AdamW config; not yet qualified for real training')
        optimizer=state['optimizer'];need(set(optimizer)=={'step','config','m','v'},'full optimizer')
        need(type(optimizer['step']) is int and optimizer['step']>=0 and optimizer['config']==identity['optimizer_config'],'optimizer metadata')
        validate_records(state['parameters'],identity['shapes'])
        validate_records(optimizer['m'],identity['shapes'])
        validate_records(optimizer['v'],identity['shapes'],True)
        rng=random.Random(0);rng.setstate(tuples(state['rng_state']))
    def thaw(self,records):
        return {k:self.np.frombuffer(bytes.fromhex(r['hex']),dtype='<f8').copy().reshape(r['shape']) for k,r in records.items()}
    def freeze(self,arrays):
        return {k:{'shape':list(a.shape),'dtype':'<f8','hex':a.astype('<f8',copy=False).tobytes(order='C').hex()} for k,a in arrays.items()}
    def train(self,state,records,weight,identity):
        np=self.np;p=self.thaw(state['parameters']);old=state['optimizer']
        optimizer={'step':old['step'],'config':dict(old['config']),'m':self.thaw(old['m']),'v':self.thaw(old['v'])}
        gradients={k:np.zeros_like(v) for k,v in p.items()}
        # Four conditions per scene checked by outer policy. Each call retains full-pool objective.
        batch_loss=0.0
        for r in records:
            loss,g,_=self.model.pool_loss_and_gradients(np,self.gru,self.objective,p,r['context'],r['candidates'],r['teacher_scores'],r['gold_index'])
            need(math.isfinite(float(loss)),'finite pool loss');batch_loss+=float(loss)*weight
            need(set(g)==set(gradients),'complete model gradients')
            for k in gradients: gradients[k]+=g[k]*weight
        updated,opt,_=self.adamw.step(np,p,gradients,optimizer,frozenset(identity['decay_keys']))
        return {'parameters':self.freeze(updated),'optimizer':{'step':opt['step'],'config':dict(opt['config']),'m':self.freeze(opt['m']),'v':self.freeze(opt['v'])},'rng_state':state['rng_state'],'cursor':state['cursor']},{'batch_loss':batch_loss,'pool_count':len(records)}
    def evaluate(self,state,records,identity):
        p=self.thaw(state['parameters']);result=[]
        for r in records:
            scores,_=self.model.forward(self.np,self.gru,p,r['context'],r['candidates'])
            need(all(math.isfinite(float(s)) for s in scores),'finite DEV scores')
            result.append({'id':r['id'],'scores':[float(s) for s in scores]})
        return result
