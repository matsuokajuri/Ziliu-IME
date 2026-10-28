"""Three-step NumPy AdamW vs independent scalar oracle; no backend import."""
import math
import adamw_numpy as a
import adamw_scalar_reference as oracle

def signature(p,s):
    return ({k:v.tobytes() for k,v in p.items()},{k:v.tobytes() for k,v in s['m'].items()},{k:v.tobytes() for k,v in s['v'].items()},s['step'],dict(s['config']))

def run(np,observe):
    errors=[]
    for index,gdata in enumerate(({'weight':[3.,4.],'bias':[-2.]},{'weight':[-.2,.1],'bias':[.3]},{'weight':[0.,-.4],'bias':[-.5]})):
        observe('issued',{'index':index,'kind':'adamw-scalar-step'})
        if index==0:
            p={'weight':np.array([1.,-2.],dtype='float64'),'bias':np.array([.5],dtype='float64')};state=a.initial(np,p)
            op={k:v.tolist() for k,v in p.items()};om={k:[0.]*len(v) for k,v in op.items()};ov={k:[0.]*len(v) for k,v in op.items()};t=0
        grad={k:np.array(v,dtype='float64') for k,v in gdata.items()};before=signature(p,state);grad_before={k:v.tobytes() for k,v in grad.items()}
        q,next_state,_=a.step(np,p,grad,state,frozenset(('weight',)))
        if signature(p,state)!=before or {k:v.tobytes() for k,v in grad.items()}!=grad_before:raise AssertionError('optimizer mutated caller arrays')
        op,om,ov,t=oracle.transition(op,gdata,om,ov,t,{'weight'})
        error=0.
        for actual,expected in ((q,op),(next_state['m'],om),(next_state['v'],ov)):
            for key in actual:
                for v,w in zip(actual[key].tolist(),expected[key]):
                    error=max(error,abs(v-w))
                    if not math.isfinite(v) or abs(v-w)>1e-14+1e-12*abs(w):raise AssertionError('AdamW scalar oracle mismatch')
        if next_state['step']!=t:raise AssertionError('step bias correction drift')
        # Fresh copies after step2 mimic nonzero moment state restoration.
        p=q;state=next_state
        if index==1:p={k:v.copy() for k,v in p.items()};state={'step':state['step'],'config':dict(state['config']),'m':{k:v.copy() for k,v in state['m'].items()},'v':{k:v.copy() for k,v in state['v'].items()}}
        errors.append(error);observe('completed',{'index':index,'kind':'adamw-scalar-step'})
    for case in (0,1):
        index=3+case;observe('issued',{'index':index,'kind':'expected-joint-alias-rejection'})
        bad={'step':state['step'],'config':dict(state['config']),'m':dict(state['m']),'v':dict(state['v'])}
        if case==0:bad['m']['bias']=p['bias']
        else:bad['v']['bias']=p['weight'][:1]
        try:a.step(np,p,{k:np.zeros_like(v) for k,v in p.items()},bad,frozenset(('weight',)))
        except ValueError as error:
            if 'alias' not in str(error):raise
        else:raise AssertionError('joint parameter/moment alias accepted')
        observe('completed',{'index':index,'kind':'expected-joint-alias-rejection'})
    return {'numeric_requests':5,'optimizer_step_attempts':5,'optimizer_step_completed':3,'expected_alias_rejections':2,'status':'THREE_ADAMW_STEPS_PENDING_REVIEW','updates':3,'maximum_absolute_errors':errors,'nonzero_moment_fresh_object_copy_after_step2':True,'real_data_rows':0}
