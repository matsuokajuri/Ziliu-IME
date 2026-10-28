"""Injected NumPy float64 AdamW, pure out-of-place state transitions.
Equation reference: https://docs.pytorch.org/docs/2.14/generated/torch.optim.AdamW.html
No backend import, checkpoint loader, RNG, training loop or execution permission.
"""
import math
CONFIG={'learning_rate':0.03,'beta1':0.9,'beta2':0.999,'epsilon':1e-8,'weight_decay':0.01,'clip_norm':1.0}

def need(value,message):
    if not value:raise ValueError(message)
def valid_array(np,a,shape):
    need(type(a)is np.ndarray and a.dtype==np.dtype('float64') and a.shape==shape and bool(np.isfinite(a).all()),'finite exact float64 geometry')
def validate(np,parameters):
    need(type(parameters)is dict and len(parameters)>0 and all(type(k)is str for k in parameters),'named parameters')
    for value in parameters.values():
        need(type(value)is np.ndarray and value.size>0,'nonempty arrays');valid_array(np,value,value.shape)
    values=list(parameters.values())
    for i,a in enumerate(values):
        for b in values[:i]:need(not np.shares_memory(a,b),'parameters must not alias')
def disjoint(np,arrays):
    for i,a in enumerate(arrays):
        for b in arrays[:i]:need(not np.shares_memory(a,b),'joint parameter/moment buffers must not alias')
def snapshot(mappings):
    return tuple(tuple((k,v.shape,str(v.dtype),v.tobytes()) for k,v in sorted(mapping.items())) for mapping in mappings)
def initial(np,parameters):
    validate(np,parameters)
    state={'step':0,'config':dict(CONFIG),'m':{k:np.zeros_like(v) for k,v in parameters.items()},'v':{k:np.zeros_like(v) for k,v in parameters.items()}}
    disjoint(np,list(parameters.values())+list(state['m'].values())+list(state['v'].values()));return state
def step(np,parameters,gradients,state,decay_keys):
    validate(np,parameters);need(set(gradients)==set(parameters),'exact gradient names')
    need(type(state)is dict and set(state)=={'step','config','m','v'},'exact optimizer state')
    need(type(state['step'])is int and 0<=state['step']<1000000,'bounded optimizer step')
    need(state['config']==CONFIG and all(type(state['config'][k])is float for k in CONFIG),'fixed optimizer hyperparameters')
    need(type(decay_keys)is frozenset and decay_keys<=set(parameters),'explicit decay names')
    for key,value in parameters.items():
        for mapping in (gradients,state['m'],state['v']):
            need(set(mapping)==set(parameters),'exact moment names');valid_array(np,mapping[key],value.shape)
        need(bool((state['v'][key]>=0).all()),'nonnegative second moment')
    old_arrays=list(parameters.values())+list(state['m'].values())+list(state['v'].values())
    disjoint(np,old_arrays)
    before=snapshot((parameters,gradients,state['m'],state['v']))
    # Stable global L2 accumulation using the stdlib scaled hypot algorithm.
    norm=math.hypot(*(float(v) for k in sorted(gradients) for v in gradients[k].flat))
    need(math.isfinite(norm),'finite global gradient norm')
    scale=1.0 if norm<=CONFIG['clip_norm'] else CONFIG['clip_norm']/norm
    t=state['step']+1;b1,b2=CONFIG['beta1'],CONFIG['beta2'];lr=CONFIG['learning_rate'];eps=CONFIG['epsilon'];wd=CONFIG['weight_decay']
    updated={};m={};v={}
    with np.errstate(over='raise',divide='raise',invalid='raise',under='ignore'):
        for key in sorted(parameters):
            grad=gradients[key]*scale
            m[key]=b1*state['m'][key]+(1-b1)*grad
            v[key]=b2*state['v'][key]+(1-b2)*grad*grad
            corrected_m=m[key]/(1-b1**t);corrected_v=v[key]/(1-b2**t)
            decayed=parameters[key]*(1-lr*wd) if key in decay_keys else parameters[key].copy()
            updated[key]=decayed-lr*corrected_m/(np.sqrt(corrected_v)+eps)
    validate(np,updated)
    for key in parameters:
        valid_array(np,m[key],parameters[key].shape);valid_array(np,v[key],parameters[key].shape)
    new_arrays=list(updated.values())+list(m.values())+list(v.values());disjoint(np,new_arrays)
    for new in new_arrays:
        for old in old_arrays+list(gradients.values()):need(not np.shares_memory(new,old),'new state must not alias any input buffer')
    need(snapshot((parameters,gradients,state['m'],state['v']))==before,'out-of-place preserves all caller arrays byte-exact')
    return updated,{'step':t,'config':dict(CONFIG),'m':m,'v':v},{'unclipped_global_norm':norm,'clip_scale':scale}
