"""Pure stdlib fake/provenance/state tests; no data, NumPy or model calls."""
import ast,copy,json,struct,types,unittest
from pathlib import Path
from unittest.mock import patch
import state_codec as codec
import initializer,label_contract as labels,session_gate as gates,backend,adamw_profile_v1 as adamw
H='a'*64
class FakeBackend:
    def input_digest(self,r):return codec.digest(r)
class FakeLoop:
    def __init__(self,scope):
        self.unknown=False;self.closed=False;self.state={'cursor':{'completed_updates':0},'all':'fake state'};self.tip=H;self.backend=FakeBackend()
        self.identity={'conditions':scope['conditions']};self.policy={'teacher_domain':scope['teacher_domain'],'train_batches':[[r['id'] for r in scope['records']]],'records':{r['id']:{'split':'TRAIN','scene':scope['scene'],'condition':r['condition'],'payload_sha256':r['payload_sha256']}for r in scope['records']}}
        self.calls=0
    def update(self,records):
        self.calls+=1;self.state={'cursor':{'completed_updates':1},'all':'fake updated'};self.tip='b'*64;return {'checkpoint_sha256':self.tip}

def fixture():
    records=[{'id':str(i),'context':[[0.]],'candidates':[[[1.]],[[2.]]],'teacher_scores':[-1.,-2.],'gold_index':None}for i in range(4)]
    scope={'schema':'ONE_COMPLETE_TRAIN_SCENE_V1','scene':'synthetic','split':'TRAIN','conditions':['a','b','c','d'],'max_student_updates':1,'data_namespace':'SYNTHETIC_QUALIFICATION_V1','teacher_namespace':'SYNTHETIC_FIXED_TARGETS_V1','teacher_domain':'synthetic-seen','selection_manifest_sha256':H,'split_manifest_sha256':H,'final_exclusion_policy_sha256':H,**{k:H for k in ('teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256')},'records':[{'id':r['id'],'condition':chr(97+i),'candidate_keys':['c'*64,'d'*64],'candidate_input_sha256s':['e'*64,'f'*64],'payload_sha256':codec.digest(r)}for i,r in enumerate(records)]}
    lab={'scope_sha256':codec.digest(scope),**{k:scope[k]for k in ('teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256')},'data_namespace':scope['data_namespace'],'teacher_namespace':scope['teacher_namespace'],'status':'INDEPENDENTLY_CLOSED_COMPLETE','unknown_requests':0,'records':[{**x,'scene':'synthetic','split':'TRAIN','score_fp32_hex':[struct.pack('<f',v).hex()for v in records[i]['teacher_scores']],'score_evidence_sha256s':[H,H]}for i,x in enumerate(scope['records'])]}
    return scope,lab,records

def backup(loop,generation):
    return {'status':'REMOTE_READBACK_FRESH_RESTORE_HEAD_CAS_VERIFIED','contract_sha256':H,'checkpoint_sha256':loop.tip,'state_sha256':codec.digest(loop.state),'step':loop.state['cursor']['completed_updates'],'archive_sha256':H,'remote_readback_archive_sha256':H,'fresh_restore_state_sha256':codec.digest(loop.state),'head_before_generation':generation,'head_after_generation':generation+1,'head_sha256':H,'head_readback_sha256':H,'unknown_operations':0,'previous_head_sha256':H}

class Contracts(unittest.TestCase):
    def test_complete_labels(self):
        s,l,r=fixture();self.assertTrue(labels.validate_labels(s,codec.digest(s),l,codec.digest(l),r,FakeBackend()))
    def test_scope_rejects_dev_final_or_two_updates(self):
        for k,v in [('split','DEV'),('split','FINAL'),('max_student_updates',2)]:
            s,_,_=fixture();s[k]=v
            with self.subTest(k=k,v=v),self.assertRaises(ValueError):labels.validate_scope(s,codec.digest(s))
    def test_unknown_staged_and_wrong_teacher_rejected(self):
        for k,v in [('status','STAGED'),('unknown_requests',1),('teacher_identity_sha256','b'*64)]:
            s,l,r=fixture();l[k]=v
            with self.subTest(k=k),self.assertRaises(ValueError):labels.validate_labels(s,codec.digest(s),l,codec.digest(l),r,FakeBackend())
    def test_incomplete_or_reordered_pool_denied(self):
        s,l,r=fixture();l['records'][0]['candidate_keys'].reverse()
        with self.assertRaises(ValueError):labels.validate_labels(s,codec.digest(s),l,codec.digest(l),r,FakeBackend())
    def test_payload_modified_denied(self):
        s,l,r=fixture();r[0]['gold_index']=1
        with self.assertRaises(ValueError):labels.validate_labels(s,codec.digest(s),l,codec.digest(l),r,FakeBackend())
    def test_score_must_be_exact_fp32_not_roundable(self):
        s,l,r=fixture();r[0]['teacher_scores'][0]=-1.-1e-10
        with self.assertRaises(ValueError):labels.validate_labels(s,codec.digest(s),l,codec.digest(l),r,FakeBackend())
    def test_nan_positive_score_denied(self):
        for value in [float('nan'),float('inf'),0.1]:
            with self.subTest(v=value),self.assertRaises(ValueError):labels.fp32(struct.pack('<f',value).hex())
    def test_shared_key_value_conflict_denied(self):
        s,l,r=fixture();r[1]['teacher_scores'][0]=-3.;s['records'][1]['payload_sha256']=codec.digest(r[1]);l['scope_sha256']=codec.digest(s);l['records'][1]['payload_sha256']=codec.digest(r[1]);l['records'][1]['score_fp32_hex'][0]=struct.pack('<f',-3.).hex()
        with self.assertRaises(ValueError):labels.validate_labels(s,codec.digest(s),l,codec.digest(l),r,FakeBackend())
    def test_genesis_remote_and_head_required(self):
        s,l,r=fixture();loop=FakeLoop(s);g=gates.OneSceneSession(loop,s,codec.digest(s),H,30,H,'SYNTHETIC_QUALIFICATION_V1')
        with self.assertRaises(ValueError):g.update(r,l,codec.digest(l))
        self.assertEqual(loop.calls,0);p=backup(loop,30);g.accept_verified_backup(p,codec.digest(p));g.update(r,l,codec.digest(l));self.assertEqual(g.status(),'BACKUP_AND_HEAD_REQUIRED')
        with self.assertRaises(ValueError):g.update(r,l,codec.digest(l))
        p=backup(loop,31);g.accept_verified_backup(p,codec.digest(p));self.assertEqual(g.status(),'PILOT_COMPLETE')
        with self.assertRaises(ValueError):g.update(r,l,codec.digest(l))
    def test_pending_backup_wrong_generation_and_old_state_denied(self):
        for k,v in [('status','UPLOADED'),('unknown_operations',1),('head_after_generation',35),('fresh_restore_state_sha256','c'*64),('remote_readback_archive_sha256','c'*64)]:
            s,_,_=fixture();loop=FakeLoop(s);p=backup(loop,30);p[k]=v
            with self.subTest(k=k),self.assertRaises(ValueError):gates.validate_backup(p,codec.digest(p),H,loop.tip,loop.state,30,H)
    def test_update_error_poisoned_no_replay(self):
        s,l,r=fixture();loop=FakeLoop(s);g=gates.OneSceneSession(loop,s,codec.digest(s),H,0,H,'SYNTHETIC_QUALIFICATION_V1');p=backup(loop,0);g.accept_verified_backup(p,codec.digest(p))
        loop.update=lambda r:(_ for _ in ()).throw(RuntimeError('synthetic issued failure'))
        with self.assertRaises(RuntimeError):g.update(r,l,codec.digest(l))
        self.assertEqual(g.status(),'UNKNOWN_NO_REPLAY')
        with self.assertRaises(ValueError):g.accept_verified_backup(p,codec.digest(p))
    def test_seals_are_not_optional(self):
        s,l,r=fixture()
        with self.assertRaises(ValueError):labels.validate_labels(s,H,l,codec.digest(l),r,FakeBackend())
    def test_profile_is_proposal_not_toy(self):
        self.assertEqual(adamw.CONFIG['learning_rate'],.001);self.assertEqual(initializer.DEFAULT_SEED,20261028)
        with self.assertRaises(ValueError):backend.StudentBackend(None,None,None,None,types.SimpleNamespace(CONFIG={**adamw.CONFIG,'learning_rate':.03}),20261028)
    def test_adamw_source_only_config_diff(self):
        here=Path(__file__).parent;old=ast.parse((here.parent/'numpy_student'/'adamw_numpy.py').read_text());new=ast.parse((here/'adamw_profile_v1.py').read_text())
        old.body[0]=new.body[0] # documented module docstring change
        for tree in (old,new):
            n=next(n for n in tree.body if isinstance(n,ast.Assign)and isinstance(n.targets[0],ast.Name)and n.targets[0].id=='CONFIG');n.value=ast.Constant(value='PROFILE')
        self.assertEqual(ast.dump(old,include_attributes=False),ast.dump(new,include_attributes=False))
    def test_no_numeric_import_or_execution_entrypoint(self):
        for p in Path(__file__).parent.glob('*.py'):
            if p.name.startswith('test_'):continue
            tree=ast.parse(p.read_text())
            for n in ast.walk(tree):
                if isinstance(n,ast.Import):self.assertFalse(any(x.name.split('.')[0]in('numpy','torch')for x in n.names))
                if isinstance(n,ast.ImportFrom):self.assertNotIn(n.module,('numpy','torch'))
    def test_backend_fullpool_one_update_wiring(self):
        calls=[]
        def loss(*args):calls.append(('pool',len(args[5])));return 4.,{'p':2.},[]
        def step(np,p,g,opt,decay):calls.append(('step',dict(g)));return {'p':p['p']-g['p']},{**opt,'step':1},{}
        b=backend.StudentBackend(types.SimpleNamespace(zeros_like=lambda v:0.),types.SimpleNamespace(pool_loss_and_gradients=loss),None,None,types.SimpleNamespace(CONFIG=adamw.CONFIG,step=step),20261028)
        b.validate_state=lambda *x:None
        state={'parameters':{'p':5.},'optimizer':{'step':0,'config':adamw.CONFIG,'m':{'p':0.},'v':{'p':0.}},'rng_state':{'test':1},'cursor':{'completed_updates':0,'next_batch':0}}
        records=[{'context':[],'candidates':[1,2,3],'teacher_scores':[-1.,-2.,-3.],'gold_index':None}for _ in range(4)]
        with patch.object(backend,'thaw',side_effect=lambda n,a:dict(a)),patch.object(backend,'freeze',side_effect=lambda n,a:dict(a)):
            out,metric=b.train(state,records,.25,{'decay_keys':['p']})
        self.assertEqual(calls,[('pool',3)]*4+[('step',{'p':2.})]);self.assertEqual(out['parameters']['p'],3.);self.assertEqual(metric,{'batch_loss':4.,'pool_count':4});self.assertEqual(out['rng_state'],state['rng_state'])
    def test_rng_schema(self):
        p={'bit_generator':'PCG64','state':{'state':1,'inc':3},'has_uint32':0,'uinteger':0};codec.validate_pcg(p)
        p['state']['inc']=2
        with self.assertRaises(ValueError):codec.validate_pcg(p)
    def test_checkpoint_excludes_embeddings_and_bad_moments(self):
        a={'shape':[1],'dtype':'<f8','hex':struct.pack('<d',1.).hex()};codec.validate_arrays({'w':a},{'w':[1]})
        with self.assertRaises(ValueError):codec.validate_arrays({'w':a,'E':a},{'w':[1]})
        a['hex']=struct.pack('<d',-1.).hex()
        with self.assertRaises(ValueError):codec.validate_arrays({'w':a},{'w':[1]},True)
if __name__=='__main__':unittest.main()
