"""Injected NumPy/GRU complete-pool student; no imports, I/O or training loop.

Frozen embeddings are caller-supplied vectors, never trainable parameters.
Backward recomputes ONE candidate sequence at a time to bound primitive caches.
All arithmetic is float64 research arithmetic; no production/quality claim.
"""
GRU_KEYS=('weight_ih','weight_hh','bias_ih','bias_hh')

def need(value,message):
    if not value:raise ValueError(message)

def shapes(embedding_width=192,hidden_width=128):
    need(type(embedding_width)is int and type(hidden_width)is int and min(embedding_width,hidden_width)>0,'positive exact integer geometry')
    d,h=embedding_width,hidden_width
    out={'projection':(d,h),'head_weight':(4*h+2,h),'head_bias':(h,),'output_weight':(h,),'output_bias':(1,)}
    for prefix in ('context','candidate'):
        for key,shape in zip(GRU_KEYS,((3*h,h),(3*h,h),(3*h,),(3*h,))):out[prefix+'_'+key]=shape
    return out

def parameter_count(embedding_width=192,hidden_width=128):
    return sum((s[0]*s[1] if len(s)==2 else s[0]) for s in shapes(embedding_width,hidden_width).values())

def check(np,x,shape):
    need(type(x)is np.ndarray and x.shape==shape and x.dtype==np.dtype('float64'),'exact float64 geometry')
    need(bool(np.isfinite(x).all()),'finite values')

def check_parameters(np,parameters,d,h):
    expected=shapes(d,h);need(type(parameters)is dict and set(parameters)==set(expected),'exact trainable parameter names')
    for k,s in expected.items():check(np,parameters[k],s)

def cell_parameters(parameters,prefix):
    return {k:parameters[prefix+'_'+k] for k in GRU_KEYS}

def forward(np,gru,parameters,context_embeddings,candidate_embeddings):
    """Returns one complete supported-pool score vector and isolated backward cache.

    Inputs are unpadded embeddings, context includes BOS as supplied upstream.
    No gold, candidate index, rank or teacher score is a forward input.
    """
    need(type(context_embeddings)is np.ndarray and context_embeddings.ndim==2,'context matrix')
    length,d=context_embeddings.shape
    need(1<=length<=64 and d>0,'context length bounds')
    need(type(candidate_embeddings)is list and 1<=len(candidate_embeddings)<=132,'whole supported pool')
    need(type(parameters)is dict and 'projection' in parameters and type(parameters['projection'])is np.ndarray and parameters['projection'].ndim==2,'projection matrix')
    h=parameters['projection'].shape[1];check_parameters(np,parameters,d,h)
    check(np,context_embeddings,(length,d))
    for tail in candidate_embeddings:
        need(type(tail)is np.ndarray and tail.ndim==2 and 1<=len(tail)<=63 and length+len(tail)<=64,'complete unpadded candidate length')
        check(np,tail,(len(tail),d))
    p={k:v.copy() for k,v in parameters.items()}
    context=context_embeddings.copy();tails=[x.copy() for x in candidate_embeddings]
    with np.errstate(over='raise',divide='raise',invalid='raise',under='ignore'):
        states,context_cache=gru.sequence_forward(np,cell_parameters(p,'context'),context@p['projection'],np.zeros(h,dtype='float64'))
        c=states[-1];scores=[];features=[];hidden=[]
        for tail in tails:
            states,candidate_cache=gru.sequence_forward(np,cell_parameters(p,'candidate'),tail@p['projection'],c)
            last=states[-1];mean=states.mean(axis=0)
            feature=np.concatenate((c,last,mean,c*last,np.array([len(tail)/63.0,length/64.0],dtype='float64')))
            activation=np.tanh(feature@p['head_weight']+p['head_bias'])
            scores.append(activation@p['output_weight']+p['output_bias'][0]);features.append(feature);hidden.append(activation)
            # The frozen GRU primitive copies matrices per timestep. Never retain
            # all candidates' caches concurrently; recompute each in backward.
            del candidate_cache
        scores=np.array(scores,dtype='float64');check(np,scores,(len(tails),))
    return scores,{'parameters':p,'context':context,'tails':tails,'context_cache':context_cache,'context_state':c.copy(),'features':features,'hidden':hidden,'width':h}

def backward(np,gru,cache,score_gradients):
    """Sum all candidate h0 AND head-feature paths before context BPTT.

    No embedding gradients are returned: the pretrained table remains frozen.
    Candidate forward recomputation is explicit and must be budgeted by callers.
    """
    need(type(cache)is dict and set(cache)=={'parameters','context','tails','context_cache','context_state','features','hidden','width'},'exact forward cache')
    p=cache['parameters'];context=cache['context'];tails=cache['tails'];h=cache['width'];d=context.shape[1]
    check_parameters(np,p,d,h);check(np,score_gradients,(len(tails),))
    need(len(cache['features'])==len(cache['hidden'])==len(tails),'complete cached pool')
    c=cache['context_state'];check(np,c,(h,))
    gradients={k:np.zeros(s,dtype='float64') for k,s in shapes(d,h).items()};dc=np.zeros(h,dtype='float64')
    with np.errstate(over='raise',divide='raise',invalid='raise',under='ignore'):
        for i,tail in enumerate(tails):
            a=cache['hidden'][i];f=cache['features'][i];check(np,a,(h,));check(np,f,(4*h+2,));ds=score_gradients[i]
            gradients['output_weight']+=ds*a;gradients['output_bias'][0]+=ds
            da=(ds*p['output_weight'])*(1.0-a*a)
            gradients['head_weight']+=np.outer(f,da);gradients['head_bias']+=da
            df=p['head_weight']@da;last=f[h:2*h]
            dc+=df[:h]+df[3*h:4*h]*last
            dl=df[h:2*h]+df[3*h:4*h]*c;dm=df[2*h:3*h]
            states,candidate_cache=gru.sequence_forward(np,cell_parameters(p,'candidate'),tail@p['projection'],c)
            check(np,states,(len(tail),h))
            grad_states=np.tile(dm/len(tail),(len(tail),1));grad_states[-1]+=dl
            dg,dx,dh0=gru.sequence_backward(np,candidate_cache,grad_states)
            for k in GRU_KEYS:gradients['candidate_'+k]+=dg[k]
            gradients['projection']+=tail.T@dx;dc+=dh0
            del candidate_cache
        context_grads=np.zeros((len(context),h),dtype='float64');context_grads[-1]=dc
        dg,dx,unused_h0=gru.sequence_backward(np,cache['context_cache'],context_grads)
        for k in GRU_KEYS:gradients['context_'+k]+=dg[k]
        gradients['projection']+=context.T@dx
    check_parameters(np,gradients,d,h)
    return gradients

def pool_loss_and_gradients(np,gru,objective,parameters,context_embeddings,candidate_embeddings,teacher_scores,gold_index=None):
    """Training-only adapter to independently pinned T2 KL + 0.5 CE reference."""
    scores,cache=forward(np,gru,parameters,context_embeddings,candidate_embeddings)
    loss,ds=objective.objective_and_score_gradient([float(s) for s in scores],teacher_scores,gold_index)
    gradients=backward(np,gru,cache,np.array(ds,dtype='float64'))
    return loss,gradients,scores

def scene_condition_weight(scene_count,conditions_in_scene):
    need(type(scene_count)is int and scene_count>0 and type(conditions_in_scene)is int and conditions_in_scene==4,'complete equal-scene four-condition batch')
    return 1.0/(scene_count*conditions_in_scene)
