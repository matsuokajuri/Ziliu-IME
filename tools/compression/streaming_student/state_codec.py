"""Stdlib-only exact student state validation. No embeddings in checkpoint schema."""
import hashlib,json,math,struct

def need(ok,message):
    if not ok:raise ValueError(message)
def canonical(x):return json.dumps(x,sort_keys=True,separators=(',',':'),allow_nan=False)
def digest(x):return hashlib.sha256(canonical(x).encode()).hexdigest()
def sha(x):return type(x)is str and len(x)==64 and x!='0'*64 and all(c in '0123456789abcdef' for c in x)
def uint(x,bits):return type(x)is int and 0<=x<2**bits

def validate_arrays(arrays,shapes,nonnegative=False):
    need(type(arrays)is dict and set(arrays)==set(shapes),'exact arrays; no E or extra state')
    for name,shape in shapes.items():
        need(type(shape)is list and shape and all(type(n)is int and n>0 for n in shape),'shapes')
        r=arrays[name];need(type(r)is dict and set(r)=={'shape','dtype','hex'},'array schema')
        need(type(r['shape'])is list and bool(r['shape']) and all(type(n)is int and n>0 for n in r['shape']),'record exact positive integer dimensions')
        need(r['shape']==shape and type(r['dtype'])is str and r['dtype']=='<f8' and type(r['hex'])is str,'float64 schema')
        raw=bytes.fromhex(r['hex']);need(len(raw)==8*math.prod(shape) and raw.hex()==r['hex'],'exact canonical bytes')
        need(all(math.isfinite(v) and (not nonnegative or v>=0) for(v,)in struct.iter_unpack('<d',raw)),'finite arrays/nonnegative v')

def validate_pcg(s):
    need(type(s)is dict and set(s)=={'bit_generator','state','has_uint32','uinteger'},'full PCG64 state')
    need(s['bit_generator']=='PCG64' and type(s['state'])is dict and set(s['state'])=={'state','inc'},'PCG64 schema')
    need(uint(s['state']['state'],128) and uint(s['state']['inc'],128) and s['state']['inc']%2==1,'PCG64 uint128 and odd stream')
    need(type(s['has_uint32'])is int and s['has_uint32']in(0,1) and uint(s['uinteger'],32),'PCG cached uint32')

def validate_rng(rng):
    need(type(rng)is dict and set(rng)=={'algorithm','seed','initialization_before','generator_state','draw_count','profile_sha256'},'complete initialization RNG')
    need(rng['algorithm']=='PCG64' and uint(rng['seed'],64) and type(rng['draw_count'])is int and rng['draw_count']>=0 and sha(rng['profile_sha256']),'RNG metadata')
    validate_pcg(rng['initialization_before']);validate_pcg(rng['generator_state'])

def validate_state(state,shapes,optimizer_config,initialization_profile_sha256,seed):
    need(type(state)is dict and set(state)=={'parameters','optimizer','rng_state','cursor'},'student-only complete state')
    opt=state['optimizer'];need(type(opt)is dict and set(opt)=={'step','config','m','v'},'complete optimizer')
    need(type(opt['step'])is int and 0<=opt['step']<1000000 and opt['config']==optimizer_config,'optimizer step/profile')
    for key,value in opt['config'].items():need(type(value)is float and math.isfinite(value),'optimizer exact finite float')
    validate_arrays(state['parameters'],shapes);validate_arrays(opt['m'],shapes);validate_arrays(opt['v'],shapes,True)
    validate_rng(state['rng_state']);need(state['rng_state']['profile_sha256']==initialization_profile_sha256 and state['rng_state']['seed']==seed,'frozen initializer and seed')
    c=state['cursor'];need(type(c)is dict and set(c)=={'completed_updates','next_batch'},'exact cursor')
    need(type(c['completed_updates'])is int and type(c['next_batch'])is int and c['completed_updates']==c['next_batch']==opt['step'],'cursor/optimizer exact')
    canonical(state)

def freeze(np,arrays):
    result={}
    for name,a in arrays.items():
        need(type(a)is np.ndarray and a.dtype==np.dtype('float64') and bool(np.isfinite(a).all()),'finite ndarray before freeze')
        result[name]={'shape':list(a.shape),'dtype':'<f8','hex':a.astype('<f8',copy=False).tobytes(order='C').hex()}
    return result

def thaw(np,records):
    return {k:np.frombuffer(bytes.fromhex(r['hex']),dtype='<f8').copy().reshape(r['shape']) for k,r in records.items()}
