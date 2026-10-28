"""Fixed synthetic learning/fresh-object resume test source. No numerical imports.
No runtime launcher. Real TRAIN, pretrained weights, teacher labels not supported.
"""
import json
import math
from pathlib import Path
import random
import gru_numpy as gru
import adamw_numpy as adam
import toy_checkpoint as ck
SEED=20261028
UPDATES=128
RESTORE_STEP=64
DECAY=frozenset(('weight_ih','weight_hh','head_weight'))
# Four literal synthetic sequences, not extracted language/user examples.
INPUTS=(((.6,.1,-.2),(.6,-.1,.2)),((-.6,.1,-.2),(-.6,-.1,.2)),((.3,-.2,.1),(.3,.2,-.1)),((-.3,-.2,.1),(-.3,.2,-.1)))
TARGETS=(.6,-.6,.3,-.3)

def init(np):
    rng=random.Random(SEED);p={}
    for key,shape in ck.SHAPES.items():
        size=math.prod(shape);p[key]=np.array([rng.uniform(-.2,.2) for _ in range(size)],dtype='float64').reshape(shape)
    return p,adam.initial(np,p),rng,{'completed_updates':0,'next_update':0}

def prediction(np,p,inputs):
    states,caches=gru.sequence_forward(np,{k:p[k] for k in gru.PARAMETERS},np.array(inputs,dtype='float64'),np.zeros(2,dtype='float64'))
    value=float(np.dot(p['head_weight'],states[-1])+p['head_bias'][0])
    if not math.isfinite(value):raise ValueError('nonfinite toy prediction')
    return value,states,caches

def evaluate(np,p):
    predictions=[prediction(np,p,x)[0] for x in INPUTS]
    return {'mse':sum((v-y)**2 for v,y in zip(predictions,TARGETS))/4,'predictions':predictions}

def update(np,p,state,rng,cursor):
    if type(state['step'])is not int or ck.canonical(cursor)!=ck.canonical({'completed_updates':state['step'],'next_update':state['step']}):raise ValueError('exact cursor/step before next update')
    order=list(range(4));rng.shuffle(order);grads={k:np.zeros_like(v) for k,v in p.items()};loss=0.
    for i in order:
        score,states,caches=prediction(np,p,INPUTS[i]);error=score-TARGETS[i];loss+=error*error/4;ds=2*error/4
        grads['head_weight']+=ds*states[-1];grads['head_bias'][0]+=ds
        dh=np.zeros_like(states);dh[-1]=ds*p['head_weight'];dp,_,_=gru.sequence_backward(np,caches,dh)
        for k in gru.PARAMETERS:grads[k]+=dp[k]
    q,next_state,stats=adam.step(np,p,grads,state,DECAY)
    return q,next_state,{'completed_updates':next_state['step'],'next_update':next_state['step']},{'loss':loss,'order':order,**stats}

def exact_state(np,p,state,rng,cursor):
    return ck.canonical({'p':ck.freeze_arrays(np,p),'m':ck.freeze_arrays(np,state['m']),'v':ck.freeze_arrays(np,state['v']),'step':state['step'],'config':state['config'],'rng':rng.getstate(),'cursor':cursor})

def run(np,root,observe,experiment_identity_sha256):
    if type(experiment_identity_sha256)is not str or len(experiment_identity_sha256)!=64 or any(c not in '0123456789abcdef' for c in experiment_identity_sha256):raise ValueError('exact externally reviewed experiment identity required')
    root=Path(root);index=0
    def request(kind,fn):
        nonlocal index
        i=index;observe('issued',{'index':i,'kind':kind});index+=1
        value=fn();observe('completed',{'index':i,'kind':kind});return value
    p,state,rng,cursor=request('initialize',lambda:init(np));initial=request('initial-evaluation',lambda:evaluate(np,p))
    path=root/'toy-step-064.json';checkpoint_sha=None;baseline_orders=[]
    for step in range(128):
        if state['step']!=step:raise ValueError('primary step mismatch')
        p,state,cursor,stats=request('primary-update-'+str(step+1),lambda:update(np,p,state,rng,cursor));baseline_orders.append(stats['order'])
        if state['step']==64:checkpoint_sha=request('save-step64',lambda:ck.save(np,path,p,state,rng,cursor,experiment_identity_sha256))
    final=request('primary-final-evaluation',lambda:evaluate(np,p));full_state=exact_state(np,p,state,rng,cursor)
    restored_rng=random.Random(0)
    rp,rs,rc=request('restore-step64',lambda:ck.restore(np,path,checkpoint_sha,experiment_identity_sha256,adam.CONFIG,restored_rng))
    for step in range(64,128):
        if rs['step']!=step:raise ValueError('restored cursor repeated or skipped step')
        rp,rs,rc,stats=request('resumed-update-'+str(step+1),lambda:update(np,rp,rs,restored_rng,rc))
        if stats['order']!=baseline_orders[step]:raise AssertionError('restored RNG shuffle mismatch')
    resumed=request('resumed-final-evaluation',lambda:evaluate(np,rp))
    if index!=198:raise AssertionError('fixed numeric request budget')
    if exact_state(np,rp,rs,restored_rng,rc)!=full_state or ck.canonical(final)!=ck.canonical(resumed):raise AssertionError('fresh-object resume not bit exact')
    passed=math.isfinite(final['mse']) and final['mse']<=.01 and final['mse']<=.1*initial['mse']
    result={'status':'TOY_LEARNING_AND_FRESH_OBJECT_RESTORE_STAGED' if passed else 'TOY_LEARNING_FIXED_THRESHOLD_REJECTED','initial':initial,'final':final,'resumed':resumed,'fixed_threshold':{'absolute_mse':.01,'relative_to_initial':.1},'learning_gate_passed':passed,'fresh_object_exact_restore':True,'fresh_process_restore_tested':False,'primary_updates':128,'resumed_updates':64,'actual_updates':192,'numeric_requests':198,'cell_forward_calls':1560,'cell_backward_calls':1536,'checkpoint_sha256':checkpoint_sha,'toy_trainable_parameters':45,'dtype':'float64','real_data_rows':0,'real_teacher_calls':0,'input_method_quality_claim':False,'new_execution_authorized':False}
    return result
