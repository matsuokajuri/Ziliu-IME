"""Numerical test source only; caller must supply separately admitted NumPy.
No automatic numerical import, no CLI runner, no real data or model weights.
"""
import math
import gru_numpy as g
EPSILON=1e-5
ATOL=1e-7
RTOL=1e-5

def snapshot(value):
    if isinstance(value,dict):return {k:snapshot(v) for k,v in value.items()}
    if isinstance(value,(list,tuple)):return [snapshot(v) for v in value]
    return (value.shape,str(value.dtype),value.tobytes())

def unchanged(value,original):
    if snapshot(value)!=original:raise AssertionError('caller or cached arrays were mutated')

def scalar_oracle(parameters,x,h):
    """Independent Python scalar/row-loop formula, no production affine helpers."""
    H=len(h);rows=[]
    for i in range(3*H):
        rows.append((sum(w*v for w,v in zip(parameters['weight_ih'][i],x))+parameters['bias_ih'][i],
                     sum(w*v for w,v in zip(parameters['weight_hh'][i],h))+parameters['bias_hh'][i]))
    result=[]
    for j in range(H):
        r=1/(1+math.exp(-sum(rows[j])));z=1/(1+math.exp(-sum(rows[H+j])))
        n=math.tanh(rows[2*H+j][0]+r*rows[2*H+j][1])
        result.append((1-z)*n+z*h[j])
    return result

def values(np,shape,offset):
    size=math.prod(shape)
    return np.array([math.sin(i+offset)*0.3 for i in range(size)],dtype='float64').reshape(shape)

def parameters(np,offset):
    return {k:values(np,shape,offset+17*i) for i,(k,shape) in enumerate(zip(g.PARAMETERS,((6,3),(6,2),(6,),(6,))))}

def finite_differences(np,arrays,expected,evaluate):
    original=snapshot(arrays);maximum=0.;coordinates=0
    for key,array in arrays.items():
        for index in np.ndindex(array.shape):
            old=float(array[index])
            try:
                array[index]=old+EPSILON;plus=evaluate()
                array[index]=old-EPSILON;minus=evaluate()
            finally:array[index]=old
            measured=(plus-minus)/(2*EPSILON);analytic=float(expected[key][index])
            if not math.isfinite(measured) or not math.isfinite(analytic):raise AssertionError('nonfinite gradient')
            error=abs(measured-analytic);maximum=max(maximum,error);coordinates+=1
            if error>ATOL+RTOL*abs(measured):raise AssertionError('gradient mismatch '+key+repr(index))
    unchanged(arrays,original)
    return {'coordinates':coordinates,'maximum_absolute_error':maximum,'epsilon':EPSILON,'atol':ATOL,'rtol':RTOL}

def run_checks(np,observe):
    """Fixed three cases. observe journals every numeric request, no silent retry.

    411 forward objective evaluations plus three analytic forward/backward
    requests =414 total numerical requests. Cell-forward budget=1680, including
    the three analytic graphs. Each request is externally guardable/journalable.
    """
    issued=completed=0
    def request(kind,operation):
        nonlocal issued,completed
        index=issued;observe('issued',{'index':index,'kind':kind});issued+=1
        value=operation();observe('completed',{'index':index,'kind':kind});completed+=1
        return value
    results={}
    # Cell:42 parameter coordinates +3 input +2 initial hidden =47.
    observe('fixture_begin',{'fixture':'cell'})
    p=parameters(np,1);x=values(np,(3,),5);h=values(np,(2,),7);up=values(np,(2,),11)
    def analytic_cell():
        inputs=(p,x,h,up);original=snapshot(inputs)
        out,cache=g.forward(np,p,x,h);cache_before=snapshot(cache);dp,dx,dh=g.backward(np,cache,up)
        unchanged(inputs,original);unchanged(cache,cache_before)
        oracle=scalar_oracle({k:v.tolist() for k,v in p.items()},x.tolist(),h.tolist())
        if max(abs(float(a)-b) for a,b in zip(out,oracle))>1e-12:raise AssertionError('scalar oracle mismatch')
        return dp,dx,dh
    dp,dx,dh=request('cell-analytic',analytic_cell)
    arrays=dict(p,x=x,h0=h);expected=dict(dp,x=dx,h0=dh)
    def cell_eval():return request('cell-forward',lambda:float(np.dot(g.forward(np,p,x,h)[0],up)))
    cell_eval();results['cell']=finite_differences(np,arrays,expected,cell_eval)
    observe('fixture_end',{'fixture':'cell'})
    # Sequence:42+3*3+2=53 coordinates; every state contributes to loss.
    observe('fixture_begin',{'fixture':'sequence'})
    p=parameters(np,23);xs=values(np,(3,3),31);h0=values(np,(2,),37);ups=values(np,(3,2),41)
    def analytic_sequence():
        inputs=(p,xs,h0,ups);original=snapshot(inputs)
        states,caches=g.sequence_forward(np,p,xs,h0);cache_before=snapshot(caches)
        result=g.sequence_backward(np,caches,ups);unchanged(inputs,original);unchanged(caches,cache_before);return result
    dp,dx,dh=request('sequence-analytic',analytic_sequence)
    def sequence_eval():return request('sequence-forward',lambda:float(np.sum(g.sequence_forward(np,p,xs,h0)[0]*ups)))
    sequence_eval();results['sequence']=finite_differences(np,dict(p,x=xs,h0=h0),dict(dp,x=dx,h0=dh),sequence_eval)
    observe('fixture_end',{'fixture':'sequence'})
    # Context->candidate chain:2*42+2*9+2=104 coordinates; direct context
    # objective plus candidate objective exercises both context gradient paths.
    observe('fixture_begin',{'fixture':'context_candidate_chain'})
    pc=parameters(np,47);pk=parameters(np,61);xc=values(np,(3,3),73);xk=values(np,(3,3),89);h0=values(np,(2,),97)
    uc=values(np,(3,2),101);uk=values(np,(3,2),109)
    def analytic_chain():
        inputs=(pc,pk,xc,xk,h0,uc,uk);original=snapshot(inputs)
        sc,cc=g.sequence_forward(np,pc,xc,h0);sk,ck=g.sequence_forward(np,pk,xk,sc[-1]);cache_before=snapshot((cc,ck))
        dpk,dxk,dinit=g.sequence_backward(np,ck,uk)
        gc=uc.copy();gc[-1]+=dinit;dpc,dxc,dh=g.sequence_backward(np,cc,gc)
        unchanged(inputs,original);unchanged((cc,ck),cache_before)
        return {**{'context_'+k:v for k,v in dpc.items()},**{'candidate_'+k:v for k,v in dpk.items()},'context_x':dxc,'candidate_x':dxk,'h0':dh}
    expected=request('chain-analytic',analytic_chain)
    arrays={**{'context_'+k:v for k,v in pc.items()},**{'candidate_'+k:v for k,v in pk.items()},'context_x':xc,'candidate_x':xk,'h0':h0}
    def chain_objective():
        sc,_=g.sequence_forward(np,pc,xc,h0);sk,_=g.sequence_forward(np,pk,xk,sc[-1]);return float(np.sum(sc*uc)+np.sum(sk*uk))
    def chain_eval():return request('chain-forward',chain_objective)
    chain_eval();results['context_candidate_chain']=finite_differences(np,arrays,expected,chain_eval)
    observe('fixture_end',{'fixture':'context_candidate_chain'})
    if [results[k]['coordinates'] for k in ('cell','sequence','context_candidate_chain')]!=[47,53,104]:raise AssertionError('gradient budget drift')
    if issued!=414 or completed!=414:raise AssertionError('request count drift')
    return {'status':'SYNTHETIC_GRU_GRADIENT_CHECKS_RETURNED_PENDING_INDEPENDENT_REVIEW','results':results,'numeric_requests_issued':issued,'numeric_requests_completed':completed,'cell_forward_calls':1680,'analytic_cell_backward_calls':10,'real_model_calls':0,'real_data_rows':0,'numpy_dtype':'float64','hidden_size':2,'input_size':3,'sequence_length':3,'new_execution_authorized':False}

if __name__=='__main__':raise SystemExit('No direct runner: separate exact source/runtime/resource numerical admission required')
