"""Independent-review reproductions: real durable loop + fake math, no NumPy."""
import copy,struct,tempfile,unittest
from test_loop_glue import training_loop,Fake
from test_contracts import fixture,backup,H,FakeLoop
from session_gate import OneSceneSession
from state_codec import digest,validate_arrays

class ReviewRegressions(unittest.TestCase):
    def test_record_shape_exact_types(self):
        for dims in ([True],[1.0],(1,),[],[0],[-1],[1,True]):
            with self.subTest(dims=dims),self.assertRaises(ValueError):
                validate_arrays({'w':{'shape':dims,'dtype':'<f8','hex':struct.pack('<d',1.).hex()}},{'w':[1]})
        validate_arrays({'w':{'shape':[1],'dtype':'<f8','hex':struct.pack('<d',1.).hex()}},{'w':[1]})
    def test_real_loop_backup_then_parameter_mutation_blocks_before_issue(self):
        scope,labels,records=fixture()
        policy={'records':{r['id']:{'split':'TRAIN','scene':scope['scene'],'condition':r['condition'],'teacher_domain':scope['teacher_domain'],'payload_sha256':r['payload_sha256']}for r in scope['records']},'final_ids':[],'teacher_domain':scope['teacher_domain'],'train_batches':[[r['id']for r in scope['records']]]}
        identity={'schema_sha256':training_loop.SCHEMA_SHA,'sources':{k:H for k in ('model','gru','objective','adamw','loop','adapter','checkpoint')},'policy_sha256':digest(policy),'shapes':{'w':[1]},'optimizer_config':{'fake':1.},'decay_keys':['w'],'conditions':scope['conditions']}
        initial={'parameters':{'w':0},'optimizer':{'step':0,'config':{'fake':1.},'m':{'w':0},'v':{'w':0}},'rng_state':{'fake':'unconsumed'},'cursor':{'completed_updates':0,'next_batch':0}}
        with tempfile.TemporaryDirectory()as directory:
            with training_loop.TrainingLoop(directory,identity,policy,Fake())as loop:
                loop.initialize(initial);g=OneSceneSession(loop,scope,digest(scope),H,0,H,'SYNTHETIC_QUALIFICATION_V1')
                p=backup(loop,0);g.accept_verified_backup(p,digest(p));tip=loop.tip
                loop.state['parameters']['w']=999
                with self.assertRaises(ValueError):g.update(records,labels,digest(labels))
                self.assertEqual(loop.tip,tip);self.assertEqual(loop.state['optimizer']['step'],0);self.assertFalse(loop.unknown)
    def test_all_accepted_anchors_rechecked(self):
        mutations=[lambda g:setattr(g.loop,'tip','b'*64),lambda g:g.loop.identity.update(extra=1),lambda g:g.loop.policy.update(extra=1),lambda g:setattr(g,'contract_sha','b'*64),lambda g:setattr(g,'scope_sha','b'*64),lambda g:g.scope.update(scene='changed'),lambda g:setattr(g,'head_sha','b'*64),lambda g:setattr(g,'generation',99)]
        for mutate in mutations:
            s,l,r=fixture();loop=FakeLoop(s);g=OneSceneSession(loop,s,digest(s),H,0,H,'SYNTHETIC_QUALIFICATION_V1');p=backup(loop,0);g.accept_verified_backup(p,digest(p));mutate(g)
            with self.assertRaises(ValueError):g.update(r,l,digest(l))
            self.assertEqual(loop.calls,0)
    def test_binding_change_before_backup_rejected(self):
        s,_,_=fixture();loop=FakeLoop(s);g=OneSceneSession(loop,s,digest(s),H,0,H,'SYNTHETIC_QUALIFICATION_V1');loop.identity.update(extra=1);p=backup(loop,0)
        with self.assertRaises(ValueError):g.accept_verified_backup(p,digest(p))
    def test_label_adapter_state_mutation_rechecked(self):
        s,l,r=fixture();loop=FakeLoop(s);g=OneSceneSession(loop,s,digest(s),H,0,H,'SYNTHETIC_QUALIFICATION_V1');p=backup(loop,0);g.accept_verified_backup(p,digest(p))
        def mutate(record):loop.state['all']='changed by adapter';return digest(record)
        loop.backend.input_digest=mutate
        with self.assertRaises(ValueError):g.update(r,l,digest(l))
        self.assertEqual(loop.calls,0)
    def test_late_loop_digest_mutations_block_before_issue_and_train(self):
        mutations={
            'parameters':lambda g:g.loop.state['parameters'].update(w=999),
            'optimizer':lambda g:g.loop.state['optimizer']['m'].update(w=999),
            'rng':lambda g:g.loop.state['rng_state'].update(fake='drift'),
            'cursor':lambda g:g.loop.state['cursor'].update(next_batch=999),
            'tip':lambda g:setattr(g.loop,'tip','b'*64),
            'identity':lambda g:g.loop.identity.update(extra=1),
            'policy':lambda g:g.loop.policy.update(extra=1),
            'scope':lambda g:g.scope.update(scene='drift'),
            'contract':lambda g:setattr(g,'contract_sha','b'*64),
            'head':lambda g:setattr(g,'head_sha','b'*64),
            'generation':lambda g:setattr(g,'generation',99),
            'backend':lambda g:setattr(g.loop,'backend',Fake()),
        }
        # Fifth call is the independent review reproduction; eighth call is the
        # final digest callback immediately before loop issues its transaction.
        for call in (5,8):
            for name,mutate in mutations.items():
                with self.subTest(call=call,anchor=name):
                    scope,labels,records=fixture()
                    policy={'records':{r['id']:{'split':'TRAIN','scene':scope['scene'],'condition':r['condition'],'teacher_domain':scope['teacher_domain'],'payload_sha256':r['payload_sha256']}for r in scope['records']},'final_ids':[],'teacher_domain':scope['teacher_domain'],'train_batches':[[r['id']for r in scope['records']]]}
                    identity={'schema_sha256':training_loop.SCHEMA_SHA,'sources':{k:H for k in ('model','gru','objective','adamw','loop','adapter','checkpoint')},'policy_sha256':digest(policy),'shapes':{'w':[1]},'optimizer_config':{'fake':1.},'decay_keys':['w'],'conditions':scope['conditions']}
                    initial={'parameters':{'w':0},'optimizer':{'step':0,'config':{'fake':1.},'m':{'w':0},'v':{'w':0}},'rng_state':{'fake':'unconsumed'},'cursor':{'completed_updates':0,'next_batch':0}}
                    with tempfile.TemporaryDirectory()as directory:
                        backend=Fake();backend.calls=0;backend.trains=0
                        original_train=backend.train
                        def train(*args):
                            backend.trains+=1
                            return original_train(*args)
                        backend.train=train
                        with training_loop.TrainingLoop(directory,identity,policy,backend)as loop:
                            loop.initialize(initial);g=OneSceneSession(loop,scope,digest(scope),H,0,H,'SYNTHETIC_QUALIFICATION_V1')
                            proof=backup(loop,0);g.accept_verified_backup(proof,digest(proof))
                            def delayed(record):
                                backend.calls+=1
                                result=digest(record)
                                if backend.calls==call:mutate(g)
                                return result
                            backend.input_digest=delayed
                            with self.assertRaises(ValueError):g.update(records,labels,digest(labels))
                            self.assertEqual(backend.calls,call)
                            self.assertEqual(backend.trains,0)
                            self.assertFalse(loop.marker(1,'issued').exists())
                            self.assertFalse(loop.checkpoint(1).exists())
                            self.assertEqual(loop.state['optimizer']['step'],0)
                            self.assertEqual(g.status(),'UNKNOWN_NO_REPLAY')
                            with self.assertRaises(ValueError):g.update(records,labels,digest(labels))
                            self.assertEqual(backend.calls,call)

if __name__=='__main__':unittest.main()
