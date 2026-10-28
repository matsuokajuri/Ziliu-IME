"""Injected initializer proposal. Import never imports NumPy or samples an RNG."""
import copy,math
from state_codec import digest,need,freeze,validate_rng
PROFILE={'name':'context-H128-D192-PCG64-uniform-v1','embedding_width':192,'hidden_width':128,'dtype':'float64','parameter_order':'sorted-name','gru_uniform_bound':'1/sqrt(H), including biases','projection_uniform_bound':'1/sqrt(D)','head_weight_uniform_bound':'1/sqrt(4H+2)','output_weight_uniform_bound':'1/sqrt(H)','head_and_output_bias':'zero','rng':'PCG64','training_rng_consumed':False}
PROFILE_SHA=digest(PROFILE)
DEFAULT_SEED=20261028

def initialize(np,model,adamw,seed=DEFAULT_SEED):
    need(type(seed)is int and 0<=seed<2**64,'explicit uint64 seed')
    shapes=model.shapes(192,128);need(model.parameter_count(192,128)==288769,'fixed H128 model geometry')
    rng=np.random.Generator(np.random.PCG64(seed));before=copy.deepcopy(rng.bit_generator.state);parameters={};draws=0
    for name,shape in sorted(shapes.items()):
        if name in ('head_bias','output_bias'):a=np.zeros(shape,dtype=np.float64)
        else:
            fan=192 if name=='projection' else 514 if name=='head_weight' else 128
            bound=1/math.sqrt(fan);a=rng.uniform(-bound,bound,size=shape).astype(np.float64);draws+=math.prod(shape)
        parameters[name]=a
    model.check_parameters(np,parameters,192,128)
    opt=adamw.initial(np,parameters)
    state={'parameters':freeze(np,parameters),'optimizer':{'step':0,'config':dict(opt['config']),'m':freeze(np,opt['m']),'v':freeze(np,opt['v'])},'rng_state':{'algorithm':'PCG64','seed':seed,'initialization_before':before,'generator_state':copy.deepcopy(rng.bit_generator.state),'draw_count':draws,'profile_sha256':PROFILE_SHA},'cursor':{'completed_updates':0,'next_batch':0}}
    validate_rng(state['rng_state']);return state
