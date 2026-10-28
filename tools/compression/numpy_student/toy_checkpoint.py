"""Synthetic-only float64 checkpoint encoding, no pickle/backend import."""
import hashlib
import json
import os
from pathlib import Path
SHAPES={'weight_ih':(6,3),'weight_hh':(6,2),'bias_ih':(6,),'bias_hh':(6,),'head_weight':(2,),'head_bias':(1,)}

def need(v,m):
    if not v:raise ValueError(m)
def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':'),allow_nan=False)
def freeze_arrays(np,arrays):
    need(set(arrays)==set(SHAPES),'exact synthetic parameter keys')
    out={}
    for k,shape in SHAPES.items():
        a=arrays[k];need(type(a)is np.ndarray and a.dtype==np.dtype('float64') and a.shape==shape and bool(np.isfinite(a).all()),'exact finite float64 checkpoint array')
        out[k]={'shape':list(shape),'dtype':'<f8','hex':a.astype('<f8',copy=False).tobytes(order='C').hex()}
    return out

def thaw_arrays(np,arrays):
    need(type(arrays)is dict and set(arrays)==set(SHAPES),'exact checkpoint keys');out={}
    for k,shape in SHAPES.items():
        r=arrays[k];need(set(r)=={'shape','dtype','hex'} and canonical(r['shape'])==canonical(list(shape)) and r['dtype']=='<f8','exact shape/type metadata')
        raw=bytes.fromhex(r['hex']);size=1
        for n in shape:size*=n
        need(len(raw)==size*8 and raw.hex()==r['hex'],'canonical exact array bytes')
        a=np.frombuffer(raw,dtype='<f8').copy().reshape(shape);need(bool(np.isfinite(a).all()),'finite restored arrays');out[k]=a
    return out

def tuples(value):return tuple(tuples(x) for x in value) if isinstance(value,list) else value

def save(np,path,parameters,state,rng,cursor,experiment_identity_sha256):
    need(type(state['step'])is int and state['step']==64 and canonical(cursor)==canonical({'completed_updates':64,'next_update':64}),'fixed completed step64 checkpoint')
    need(type(experiment_identity_sha256)is str and len(experiment_identity_sha256)==64 and all(c in '0123456789abcdef' for c in experiment_identity_sha256),'exact experiment identity digest')
    payload={'schema':'SYNTHETIC_FLOAT64_TOY_CHECKPOINT_V1','experiment_identity_sha256':experiment_identity_sha256,'parameters':freeze_arrays(np,parameters),'m':freeze_arrays(np,state['m']),'v':freeze_arrays(np,state['v']),'step':state['step'],'config':state['config'],'rng_state':rng.getstate(),'cursor':cursor}
    content=canonical(payload).encode();envelope={'payload':payload,'payload_sha256':hashlib.sha256(content).hexdigest()};data=(canonical(envelope)+'\n').encode();path=Path(path)
    with path.open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
    fd=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
    try:os.fsync(fd)
    finally:os.close(fd)
    need(path.read_bytes()==data,'exact checkpoint readback');return hashlib.sha256(data).hexdigest()

def restore(np,path,expected_file_sha,experiment_identity_sha256,config,rng):
    path=Path(path);need(path.is_file() and not path.is_symlink(),'regular non-symlink checkpoint');data=path.read_bytes();need(hashlib.sha256(data).hexdigest()==expected_file_sha,'sealed checkpoint file')
    wrapper=json.loads(data);need(set(wrapper)=={'payload','payload_sha256'},'exact checkpoint envelope');p=wrapper['payload']
    need(set(p)=={'schema','experiment_identity_sha256','parameters','m','v','step','config','rng_state','cursor'},'exact checkpoint payload')
    need(hashlib.sha256(canonical(p).encode()).hexdigest()==wrapper['payload_sha256'],'checkpoint internal digest')
    need(p['schema']=='SYNTHETIC_FLOAT64_TOY_CHECKPOINT_V1' and p['experiment_identity_sha256']==experiment_identity_sha256,'same exact toy source')
    need(type(p['step'])is int and p['step']==64 and canonical(p['cursor'])==canonical({'completed_updates':64,'next_update':64}),'no skipped or replayed completed step')
    need(canonical(p['config'])==canonical(config),'exact optimizer config')
    params=thaw_arrays(np,p['parameters']);m=thaw_arrays(np,p['m']);v=thaw_arrays(np,p['v'])
    for a in v.values():need(bool((a>=0).all()),'nonnegative variance')
    rng.setstate(tuples(p['rng_state']))
    return params,{'step':64,'config':dict(config),'m':m,'v':v},dict(p['cursor'])
