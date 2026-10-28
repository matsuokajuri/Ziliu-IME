"""Two externally frozen scalar-oracle comparisons + one overflow rejection.
Injected NumPy only; no loader, numerical import or automatic execution.
"""
import math
import gru_numpy as g
KEYS={'w_input':'weight_ih','w_recurrent':'weight_hh','b_input':'bias_ih','b_recurrent':'bias_hh'}

def run_checks(np,fixture,observe,starting_index=414):
    if starting_index!=414 or type(starting_index)is not int:raise ValueError('fixed prior request count')
    if fixture['schema']!='INDEPENDENT_STDLIB_GRU_FLOAT64_SYNTHETIC_V1' or len(fixture['cases'])!=2:raise ValueError('fixed independent fixture')
    results=[]
    for i,case in enumerate(fixture['cases']):
        name='external_oracle_'+str(i);observe('fixture_begin',{'fixture':name});index=starting_index+i
        observe('issued',{'index':index,'kind':name})
        p=case['inputs_and_parameters'];pc={KEYS[k]:np.array(v,dtype='float64') for k,v in p['context_parameters'].items()}
        xc=np.array(p['context_inputs'],dtype='float64');h0=np.array(p['context_h0'],dtype='float64')
        sc,cc=g.sequence_forward(np,pc,xc,h0)
        uc=np.array([[.17*(j+1),-.11*(j+2)] for j in range(len(sc))],dtype='float64')
        loss=float(np.sum(sc*uc));all_grad={}
        if 'candidate_parameters' in p:
            pk={KEYS[k]:np.array(v,dtype='float64') for k,v in p['candidate_parameters'].items()}
            xk=np.array(p['candidate_inputs'],dtype='float64');sk,ck=g.sequence_forward(np,pk,xk,sc[-1])
            uk=np.array([[.31*(j+1),.23*(j+1)] for j in range(len(sk))],dtype='float64');loss+=float(np.sum(sk*uk))
            dp,dx,dh=g.sequence_backward(np,ck,uk);uc[-1]+=dh
            all_grad.update(candidate_parameters={k:dp[v].tolist() for k,v in KEYS.items()},candidate_inputs=dx.tolist())
        dp,dx,dh=g.sequence_backward(np,cc,uc)
        all_grad.update(context_parameters={k:dp[v].tolist() for k,v in KEYS.items()},context_inputs=dx.tolist(),context_h0=dh.tolist())
        maximum=0.;seen=set()
        for reference in case['gradients']:
            path=reference['path'];v=all_grad
            for part in path:v=v[part]
            key=tuple(path)
            if key in seen:raise AssertionError('duplicate gradient coordinate')
            seen.add(key);error=abs(v-reference['value']);maximum=max(maximum,error)
            if not math.isfinite(v) or error>1e-7+1e-5*abs(reference['value']):raise AssertionError('independent oracle gradient mismatch')
        if len(seen)!=case['gradient_count'] or len(seen)!=(53,101)[i]:raise AssertionError('independent gradient budget')
        if abs(loss-case['loss'])>1e-12:raise AssertionError('independent scalar loss mismatch')
        result={'case_index':i,'gradient_coordinates':len(seen),'maximum_absolute_error':maximum,'loss_absolute_error':abs(loss-case['loss'])}
        observe('completed',{'index':index,'kind':name});observe('fixture_end',{'fixture':name});results.append(result)
    name='overflow_rejection';observe('fixture_begin',{'fixture':name});observe('issued',{'index':416,'kind':name})
    p={k:np.zeros(shape,dtype='float64') for k,shape in zip(g.PARAMETERS,((6,3),(6,2),(6,),(6,)))}
    p['weight_ih'][0,0]=1e308
    try:g.forward(np,p,np.array([2.,0.,0.],dtype='float64'),np.zeros(2,dtype='float64'))
    except FloatingPointError:pass
    else:raise AssertionError('finite-input affine overflow was not rejected')
    observe('completed',{'index':416,'kind':name});observe('fixture_end',{'fixture':name})
    return {'external_oracle':results,'external_oracle_numeric_requests':2,'external_oracle_cell_forward_calls':8,'external_oracle_cell_backward_calls':8,'overflow_test_requests':1,'overflow_cell_attempts':1,'overflow_rejected_before_cell_return':True,'real_model_calls':0}
