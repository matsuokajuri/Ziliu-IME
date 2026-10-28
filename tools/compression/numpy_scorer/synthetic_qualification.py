"""Eight fixed synthetic NumPy primitive fixtures; no real model or loader.
Stdlib import only. No real endpoint/base/task input; no model forward.
"""
import math
from scorer import Operations

def run(np,checkpoint):
    """Exactly8 named primitive fixture checks; BLAS/ufunc calls aren't forwards.
    checkpoint is a caller-supplied observation hook, not a security guarantee.
    """
    o=Operations(np);records=[]
    def check(name,condition):
        if not bool(condition):raise AssertionError(name)
        records.append({'name':name,'passed':True});checkpoint(name)
    checkpoint('before_synthetic')
    x=np.array([[1,2],[3,4]],dtype=np.float32);w=np.array([[1,0],[0,2]],dtype=np.float32);b=np.array([2,-1],dtype=np.float32)
    check('linear_exact',np.array_equal(o.linear(x,w,b),np.array([[3,3],[5,7]],dtype=np.float32)))
    a=np.full((2,192),3,dtype=np.float32);one=np.ones(192,dtype=np.float32);bias=np.full(192,2,dtype=np.float32)
    check('layernorm_constant_population',np.array_equal(o.layer_norm(a,one,bias),np.full((2,192),2,dtype=np.float32)))
    a=np.array([[-1,1]],dtype=np.float32)
    expected=np.array([[-1/math.sqrt(1+1e-5),1/math.sqrt(1+1e-5)]],dtype=np.float32)
    check('layernorm_population_not_sample',np.allclose(o.layer_norm(a,np.ones(2,dtype=np.float32),np.zeros(2,dtype=np.float32)),expected,rtol=0,atol=2e-7))
    a=np.array([[-8,-1,-0.0,0.0,1,8]],dtype=np.float32)
    expected=np.array([np.float32(np.float32(float(v)*.5)*np.float32(1+np.float32(math.erf(float(v)/math.sqrt(2))))) for v in a.flat],dtype=np.float32).reshape(a.shape)
    check('gelu_frozen_cast_formula',np.array_equal(o.gelu(a),expected))
    check('softmax_uniform',np.array_equal(o.softmax(np.zeros((2,4),dtype=np.float32)),np.full((2,4),.25,dtype=np.float32)))
    masked=np.array([[0,-np.inf,-np.inf],[0,0,-np.inf],[0,0,0]],dtype=np.float32);result=o.softmax(masked)
    check('causal_mask_before_softmax',np.array_equal(result[np.triu_indices(3,1)],np.zeros(3,dtype=np.float32)) and np.allclose(result.sum(axis=1),1,rtol=0,atol=1e-7))
    result=o.log_softmax(np.array([[1000,1000],[-1000,-1000]],dtype=np.float32))
    check('logsoftmax_stable',np.allclose(result,np.float32(-math.log(2)),rtol=0,atol=1e-7))
    t=3;qkv=np.arange(t*576,dtype=np.float32).reshape(t,576);q,k,v=[part.reshape(t,4,48).transpose(1,0,2) for part in np.split(qkv,3,axis=-1)]
    check('qkv_head_layout',q.shape==(4,t,48) and k[2,1,7]==np.float32(576+192+2*48+7) and v[3,2,9]==np.float32(2*576+384+3*48+9))
    return {'schema':'R59_NUMPY_PRIMITIVE_SYNTHETIC_V1','fixture_count':8,'checks':records,'real_model_loads':0,'model_forwards':0,'task_inputs_read':0,'cache_published':0}
