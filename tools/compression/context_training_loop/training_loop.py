"""Source-only injected training transaction interface. No backend imports/launcher.
A failed issued transaction is UNKNOWN, including after checkpoint publication.
No automatic replay, orphan checkpoint adoption, or journal repair is provided.
"""
import copy
import fcntl
import math
from pathlib import Path
from checkpoint_store import canonical, digest, save_new, read_sealed

SCHEMA = {'name':'CONTEXT_TRAIN_LOOP_V1','dtype':'float64',
          'state_fields':['parameters','optimizer','rng_state','cursor'],
          'cursor_fields':['completed_updates','next_batch'],
          'batch_rule':'equal scenes, exactly four conditions, full candidate pools',
          'unknown_rule':'issued without sealed complete blocks all continuation'}
SCHEMA_SHA = digest(SCHEMA)

def need(ok, message):
    if not ok: raise ValueError(message)

def sha(value):
    return type(value) is str and len(value)==64 and all(c in '0123456789abcdef' for c in value)

def validate_identity(identity):
    need(set(identity)=={'schema_sha256','sources','policy_sha256','shapes','optimizer_config','decay_keys','conditions'},'exact identity')
    need(identity['schema_sha256']==SCHEMA_SHA,'schema pin')
    need(type(identity['sources']) is dict and set(identity['sources'])=={'model','gru','objective','adamw','loop','adapter','checkpoint'},'all source pins')
    need(all(sha(s) for s in identity['sources'].values()),'source digests')
    need(sha(identity['policy_sha256']),'policy digest')
    need(type(identity['conditions']) is list and len(identity['conditions'])==4 and len(set(identity['conditions']))==4 and all(type(x) is str for x in identity['conditions']),'four condition ids')
    need(type(identity['shapes']) is dict and len(identity['shapes'])>0,'parameter schema')
    for k,shape in identity['shapes'].items():
        need(type(k) is str and type(shape) is list and shape and all(type(n) is int and n>0 for n in shape),'shape')
    need(type(identity['decay_keys']) is list and len(set(identity['decay_keys']))==len(identity['decay_keys']) and set(identity['decay_keys'])<=set(identity['shapes']),'explicit decay schema')
    canonical(identity)

def validate_policy(policy, identity):
    need(digest(policy)==identity['policy_sha256'],'pinned trusted loader policy')
    need(set(policy)=={'records','final_ids','teacher_domain','train_batches'},'exact policy')
    need(type(policy['teacher_domain']) is str and policy['teacher_domain'],'teacher seen-domain boundary')
    need(type(policy['final_ids']) is list and len(set(policy['final_ids']))==len(policy['final_ids']),'final exclusion list')
    need(type(policy['records']) is dict and not(set(policy['records']) & set(policy['final_ids'])),'final exclusion')
    for key,r in policy['records'].items():
        need(type(key) is str and set(r)=={'split','scene','condition','teacher_domain','payload_sha256'},'record provenance')
        need(r['split'] in ('TRAIN','DEV') and type(r['scene']) is str and r['condition'] in identity['conditions'],'split/condition')
        need(r['teacher_domain']==policy['teacher_domain'],'teacher domain pin');need(sha(r['payload_sha256']),'payload digest')
    train={r['scene'] for r in policy['records'].values() if r['split']=='TRAIN'}
    dev={r['scene'] for r in policy['records'].values() if r['split']=='DEV'}
    need(not train & dev,'scene-disjoint TRAIN/DEV')
    need(type(policy['train_batches']) is list and policy['train_batches'],'frozen nonempty batch schedule')
    for ids in policy['train_batches']:
        need(type(ids) is list and ids and len(set(ids))==len(ids) and all(k in policy['records'] and policy['records'][k]['split']=='TRAIN' for k in ids),'TRAIN-only schedule')

def validate_state(state, identity, backend):
    need(set(state)=={'parameters','optimizer','rng_state','cursor'},'full state schema')
    c=state['cursor'];need(set(c)=={'completed_updates','next_batch'},'cursor schema')
    need(type(c['completed_updates']) is int and c['completed_updates']>=0 and type(c['next_batch']) is int and c['next_batch']==c['completed_updates'],'exact sequential cursor')
    need(state['optimizer']['step']==c['completed_updates'],'optimizer/cursor consistent')
    backend.validate_state(state, identity)
    canonical(state)

class TrainingLoop:
    def __init__(self, directory, identity, policy, backend):
        validate_identity(identity);validate_policy(policy,identity)
        self.identity=copy.deepcopy(identity);self.policy=copy.deepcopy(policy);self.backend=backend
        self.directory=Path(directory);self.directory.mkdir(mode=0o700,exist_ok=True)
        need(not self.directory.is_symlink(),'non-symlink run directory')
        self.lock=(self.directory/'.lock').open('a+b')
        try: fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BaseException: self.lock.close();raise
        self.state=None;self.tip=None;self.closed=False;self.unknown=False;self.dev_count=0
    def close(self):
        if not self.closed: self.closed=True;self.lock.close()
    def __enter__(self): return self
    def __exit__(self,*args): self.close()
    def ensure_open(self):
        need(not self.closed,'closed loop');need(not self.unknown,'UNKNOWN transaction: reconcile externally')
    def envelope(self,state):
        return {'schema_sha256':SCHEMA_SHA,'identity_sha256':digest(self.identity),'state':state}
    def checkpoint(self,n): return self.directory/('checkpoint-%08d.json'%n)
    def marker(self,n,kind): return self.directory/('%08d.%s.json'%(n,kind))
    def initialize(self,state):
        self.ensure_open();need(self.state is None,'not initialized')
        need(set(p.name for p in self.directory.iterdir())=={'.lock'},'fresh namespace required')
        validate_state(state,self.identity,self.backend);need(state['cursor']['completed_updates']==0,'initial step zero')
        self.unknown=True
        file_sha=save_new(self.checkpoint(0),self.envelope(state))
        genesis_sha=save_new(self.directory/'genesis.json',{'checkpoint_sha256':file_sha,'identity_sha256':digest(self.identity)})
        self.state=copy.deepcopy(state);self.tip=file_sha;self.unknown=False
        return {'checkpoint_sha256':file_sha,'genesis_sha256':genesis_sha}
    def restore(self, genesis_sha):
        return self.restore_chain(genesis_sha,[])
    def restore_chain(self, genesis_sha, chain, dev_chain=()):
        """Caller supplies sealed issued/complete file SHA pairs from durable receipts."""
        self.ensure_open();need(self.state is None,'not initialized')
        genesis=read_sealed(self.directory/'genesis.json',genesis_sha)
        need(set(genesis)=={'checkpoint_sha256','identity_sha256'} and genesis['identity_sha256']==digest(self.identity),'sealed genesis')
        tip=genesis['checkpoint_sha256'];self._check_checkpoint(0,tip);valid_tips={tip};expected={'.lock','genesis.json','checkpoint-00000000.json'}
        for n,pair in enumerate(chain,1):
            need(type(pair) is dict and set(pair)=={'issued_sha256','complete_sha256'},'sealed chain pair')
            issued=read_sealed(self.marker(n,'issued'),pair['issued_sha256']);complete=read_sealed(self.marker(n,'complete'),pair['complete_sha256'])
            need(set(issued)=={'step','identity_sha256','previous_checkpoint_sha256','batch_ids'} and issued['step']==n and issued['identity_sha256']==digest(self.identity) and issued['previous_checkpoint_sha256']==tip,'issued chain')
            need(set(complete)=={'step','issued_sha256','checkpoint_sha256','metrics'} and complete['step']==n and complete['issued_sha256']==pair['issued_sha256'],'complete chain')
            self._validate_ids(issued['batch_ids'],'TRAIN')
            need(n<=len(self.policy['train_batches']) and issued['batch_ids']==self.policy['train_batches'][n-1],'exact schedule replay')
            tip=complete['checkpoint_sha256'];self._check_checkpoint(n,tip);valid_tips.add(tip)
            expected.update({self.marker(n,'issued').name,self.marker(n,'complete').name,self.checkpoint(n).name})
        for n,pair in enumerate(dev_chain,1):
            need(set(pair)=={'issued_sha256','complete_sha256'},'DEV sealed pair')
            issued=read_sealed(self.dev_marker(n,'issued'),pair['issued_sha256']);complete=read_sealed(self.dev_marker(n,'complete'),pair['complete_sha256'])
            need(set(issued)=={'evaluation','identity_sha256','checkpoint_sha256','ids'} and issued['evaluation']==n and issued['identity_sha256']==digest(self.identity) and issued['checkpoint_sha256'] in valid_tips,'DEV issued chain')
            self._validate_ids(issued['ids'],'DEV')
            need(set(complete)=={'evaluation','issued_sha256','result_sha256'} and complete['evaluation']==n and complete['issued_sha256']==pair['issued_sha256'] and sha(complete['result_sha256']),'DEV complete chain')
            expected.update({self.dev_marker(n,'issued').name,self.dev_marker(n,'complete').name})
        need(set(p.name for p in self.directory.iterdir())==expected,'UNKNOWN/extra evidence blocks continuation')
        self.dev_count=len(dev_chain)
        self._load(len(chain),tip);return copy.deepcopy(self.state)
    def _check_checkpoint(self,n,tip):
        p=read_sealed(self.checkpoint(n),tip)
        need(set(p)=={'schema_sha256','identity_sha256','state'} and p['schema_sha256']==SCHEMA_SHA and p['identity_sha256']==digest(self.identity),'checkpoint schema/source identity')
        validate_state(p['state'],self.identity,self.backend)
        need(p['state']['cursor']['completed_updates']==n,'checkpoint step')
        return p['state']
    def _load(self,n,tip): self.state=self._check_checkpoint(n,tip);self.tip=tip
    def _validate_ids(self,ids,split):
        need(type(ids) is list and ids and len(set(ids))==len(ids),'unique batch ids')
        provenance=[]
        for key in ids:
            need(key not in self.policy['final_ids'] and key in self.policy['records'],'only authorized split records')
            r=self.policy['records'][key];need(r['split']==split,'split access denied');provenance.append(r)
        if split=='TRAIN':
            scenes={r['scene'] for r in provenance}
            for scene in scenes:
                rows=[r for r in provenance if r['scene']==scene]
                need(len(rows)==4 and {r['condition'] for r in rows}==set(self.identity['conditions']),'complete four-condition scenes')
        return provenance
    def update(self,records):
        self.ensure_open();need(self.state is not None,'initialized state required')
        ids=[r['id'] for r in records];provenance=self._validate_ids(ids,'TRAIN')
        need(all(set(r)=={'id','context','candidates','teacher_scores','gold_index'} for r in records),'training payload only')
        self._validate_payloads(records)
        n=self.state['cursor']['completed_updates']+1
        need(n<=len(self.policy['train_batches']) and ids==self.policy['train_batches'][n-1],'exact frozen schedule cursor')
        # No automatic retry after any failure, even if backend was never entered.
        self.unknown=True
        issued=save_new(self.marker(n,'issued'),{'step':n,'identity_sha256':digest(self.identity),'previous_checkpoint_sha256':self.tip,'batch_ids':ids})
        candidate,metrics=self.backend.train(copy.deepcopy(self.state),copy.deepcopy(records),1.0/len(provenance),self.identity)
        need(set(metrics)=={'batch_loss','pool_count'} and type(metrics['pool_count']) is int and metrics['pool_count']==len(records) and math.isfinite(metrics['batch_loss']),'complete batch metrics')
        candidate['cursor']={'completed_updates':n,'next_batch':n}
        validate_state(candidate,self.identity,self.backend)
        tip=save_new(self.checkpoint(n),self.envelope(candidate))
        complete=save_new(self.marker(n,'complete'),{'step':n,'issued_sha256':issued,'checkpoint_sha256':tip,'metrics':metrics})
        self.state=copy.deepcopy(candidate);self.tip=tip;self.unknown=False
        return {'issued_sha256':issued,'complete_sha256':complete,'checkpoint_sha256':tip}
    def _validate_payloads(self,records):
        for r in records:
            need(self.backend.input_digest(r)==self.policy['records'][r['id']]['payload_sha256'],'exact trusted payload bytes/targets')
    def dev_marker(self,n,kind): return self.directory/('dev-%08d.%s.json'%(n,kind))
    def evaluate_dev(self,records):
        self.ensure_open();need(self.state is not None,'initialized state required')
        ids=[r['id'] for r in records];self._validate_ids(ids,'DEV')
        need(all(set(r)=={'id','context','candidates'} for r in records),'DEV forward-only; no teacher/gold targets')
        self._validate_payloads(records);n=self.dev_count+1;self.unknown=True
        issued=save_new(self.dev_marker(n,'issued'),{'evaluation':n,'identity_sha256':digest(self.identity),'checkpoint_sha256':self.tip,'ids':ids})
        # Independent copies: evaluation cannot change optimizer, RNG or cursor.
        result=self.backend.evaluate(copy.deepcopy(self.state),copy.deepcopy(records),self.identity)
        complete=save_new(self.dev_marker(n,'complete'),{'evaluation':n,'issued_sha256':issued,'result_sha256':digest(result)})
        self.dev_count=n;self.unknown=False
        return {'result':result,'receipt':{'issued_sha256':issued,'complete_sha256':complete}}
