"""Injected full-pool backend. Existing model/GRU/objective code is not modified."""
import copy,math
from state_codec import need,digest,validate_state,freeze,thaw
from initializer import PROFILE_SHA
from adamw_profile_v1 import CONFIG
OPTIMIZER_PROFILE_SHA='20ea3a95204cbd01d5e92b8c5909c59b5f8aff1f468ea8259c8bfb206e9b7639'

class StudentBackend:
    def __init__(self,np,model,gru,objective,adamw,seed):
        need(digest(adamw.CONFIG)==digest(CONFIG)==OPTIMIZER_PROFILE_SHA,'streaming optimizer proposal, never old toy lr');self.np=np;self.model=model;self.gru=gru;self.objective=objective;self.adamw=adamw;self.seed=seed
    def input_digest(self,record):
        def packed(a):
            need(type(a)is self.np.ndarray and a.dtype==self.np.dtype('float64') and a.ndim==2 and a.shape[1]==192 and bool(self.np.isfinite(a).all()),'finite frozen embedding matrices D192')
            return {'shape':list(a.shape),'dtype':'<f8','hex':a.astype('<f8',copy=False).tobytes(order='C').hex()}
        need(type(record['candidates'])is list and 1<=len(record['candidates'])<=132,'complete supported pool size')
        context=packed(record['context']);need(1<=context['shape'][0]<=64,'context includes BOS and bounded length')
        candidates=[packed(a)for a in record['candidates']]
        need(all(1<=a['shape'][0]<=63 and context['shape'][0]+a['shape'][0]<=64 for a in candidates),'unaltered complete supported candidate lengths')
        return digest({**record,'context':context,'candidates':candidates})
    def validate_state(self,state,identity):
        need(identity['shapes']=={k:list(v)for k,v in self.model.shapes(192,128).items()},'fixed H128/D192 shapes')
        need(identity['optimizer_config']==self.adamw.CONFIG and digest(self.adamw.CONFIG)==OPTIMIZER_PROFILE_SHA,'new frozen profile (not toy)')
        need(set(identity['decay_keys'])=={k for k,v in identity['shapes'].items()if len(v)==2},'rank-two-only weight decay')
        validate_state(state,identity['shapes'],self.adamw.CONFIG,PROFILE_SHA,self.seed)
        need(state['rng_state']['draw_count']==288640,'fixed initialization draw budget')
    def train(self,state,records,weight,identity):
        self.validate_state(state,identity);need(len(records)==4 and weight==0.25,'one four-condition scene only')
        np=self.np;p=thaw(np,state['parameters']);old=state['optimizer'];opt={'step':old['step'],'config':dict(old['config']),'m':thaw(np,old['m']),'v':thaw(np,old['v'])}
        gradients={k:np.zeros_like(v)for k,v in p.items()};loss_sum=0.0
        for r in records:
            loss,g,_=self.model.pool_loss_and_gradients(np,self.gru,self.objective,p,r['context'],r['candidates'],r['teacher_scores'],r['gold_index'])
            need(math.isfinite(float(loss)) and set(g)==set(gradients),'finite whole-pool loss/complete gradients')
            loss_sum+=float(loss)*0.25
            for k in gradients:gradients[k]+=g[k]*0.25
        updated,new,_=self.adamw.step(np,p,gradients,opt,frozenset(identity['decay_keys']))
        result={'parameters':freeze(np,updated),'optimizer':{'step':new['step'],'config':dict(new['config']),'m':freeze(np,new['m']),'v':freeze(np,new['v'])},'rng_state':copy.deepcopy(state['rng_state']),'cursor':copy.deepcopy(state['cursor'])}
        # Outer reviewed loop advances cursor once, then validates entire state.
        return result,{'batch_loss':loss_sum,'pool_count':4}
    def evaluate(self,*args,**kwargs):raise ValueError('DEV/final evaluation absent from one-scene pipeline pilot')
