"""Injected-NumPy float64 research GRU primitives, no eager numerical imports.

Reset-after-affine variant, gate order (r,z,n), two separate bias vectors.
Source equation: https://docs.pytorch.org/docs/2.14/generated/torch.nn.GRUCell.html
No training loop, I/O, model weights, resource admission or implicit NumPy import.
"""
PARAMETERS=('weight_ih','weight_hh','bias_ih','bias_hh')

def parameter_count(input_size,hidden_size):
    if type(input_size)is not int or type(hidden_size)is not int or min(input_size,hidden_size)<1:raise ValueError('positive integer dimensions')
    return 3*hidden_size*input_size+3*hidden_size*hidden_size+6*hidden_size

def require(value,message):
    if not value:raise ValueError(message)

def check_array(np,a,shape):
    require(type(a)is np.ndarray and a.shape==shape and a.dtype==np.dtype('float64'),'exact float64 ndarray geometry')
    require(bool(np.isfinite(a).all()),'finite array')

def check_parameters(np,parameters,d,h):
    require(type(parameters)is dict and set(parameters)==set(PARAMETERS),'exact four parameter arrays')
    for key,shape in zip(PARAMETERS,((3*h,d),(3*h,h),(3*h,),(3*h,))):check_array(np,parameters[key],shape)

def sigmoid(np,x):
    out=np.empty_like(x);positive=x>=0
    out[positive]=1.0/(1.0+np.exp(-x[positive]))
    e=np.exp(x[~positive]);out[~positive]=e/(1.0+e)
    return out

def forward(np,parameters,x,h):
    require(type(x)is np.ndarray and x.ndim==1 and type(h)is np.ndarray and h.ndim==1,'one-vector inputs')
    d,H=len(x),len(h);require(d>0 and H>0,'nonempty geometry')
    check_parameters(np,parameters,d,H);check_array(np,x,(d,));check_array(np,h,(H,))
    with np.errstate(over='raise',divide='raise',invalid='raise',under='ignore'):
        ai=parameters['weight_ih']@x+parameters['bias_ih']
        ah=parameters['weight_hh']@h+parameters['bias_hh']
        r=sigmoid(np,ai[:H]+ah[:H]);z=sigmoid(np,ai[H:2*H]+ah[H:2*H])
        q=ah[2*H:];n=np.tanh(ai[2*H:]+r*q)
        result=(1.0-z)*n+z*h;check_array(np,result,(H,))
    # Copies bind this backward to exactly the values used in this forward.
    cache={'x':x.copy(),'h':h.copy(),'r':r.copy(),'z':z.copy(),'n':n.copy(),'q':q.copy(),
           'weight_ih':parameters['weight_ih'].copy(),'weight_hh':parameters['weight_hh'].copy()}
    return result,cache

def backward(np,cache,grad_output):
    require(type(cache)is dict and set(cache)=={'x','h','r','z','n','q','weight_ih','weight_hh'},'exact forward cache')
    x,h=cache['x'],cache['h'];d,H=len(x),len(h)
    check_array(np,grad_output,(H,));check_array(np,x,(d,));check_array(np,h,(H,))
    for key in ('r','z','n','q'):check_array(np,cache[key],(H,))
    check_array(np,cache['weight_ih'],(3*H,d));check_array(np,cache['weight_hh'],(3*H,H))
    r,z,n,q=(cache[k] for k in ('r','z','n','q'))
    with np.errstate(over='raise',divide='raise',invalid='raise',under='ignore'):
        dn=grad_output*(1.0-z);dz=grad_output*(h-n)
        dan=dn*(1.0-n*n);dqr=dan*r;dar=dan*q*r*(1.0-r);daz=dz*z*(1.0-z)
        di=np.concatenate((dar,daz,dan));dh=np.concatenate((dar,daz,dqr))
        dx=cache['weight_ih'].T@di
        dh_previous=grad_output*z+cache['weight_hh'].T@dh
        gradients={'weight_ih':np.outer(di,x),'weight_hh':np.outer(dh,h),'bias_ih':di.copy(),'bias_hh':dh.copy()}
    check_parameters(np,gradients,d,H);check_array(np,dx,(d,));check_array(np,dh_previous,(H,))
    return gradients,dx,dh_previous

def sequence_forward(np,parameters,inputs,h0):
    require(type(inputs)is np.ndarray and inputs.ndim==2 and inputs.shape[0]>0,'nonempty unpadded sequence')
    check_array(np,inputs,inputs.shape);h=h0.copy();states=[];caches=[]
    for x in inputs:
        h,cache=forward(np,parameters,x,h);states.append(h);caches.append(cache)
    return np.stack(states),caches

def sequence_backward(np,caches,grad_states):
    require(type(caches)is list and len(caches)>0,'nonempty sequence caches')
    T=len(caches);H=len(caches[0]['h']);d=len(caches[0]['x']);check_array(np,grad_states,(T,H))
    params={k:np.zeros(shape,dtype='float64') for k,shape in zip(PARAMETERS,((3*H,d),(3*H,H),(3*H,),(3*H,)))}
    inputs=np.zeros((T,d),dtype='float64');carry=np.zeros(H,dtype='float64')
    for t in reversed(range(T)):
        gradient,inputs[t],carry=backward(np,caches[t],grad_states[t]+carry)
        for k in PARAMETERS:params[k]+=gradient[k]
    check_parameters(np,params,d,H);check_array(np,inputs,(T,d));check_array(np,carry,(H,))
    return params,inputs,carry
