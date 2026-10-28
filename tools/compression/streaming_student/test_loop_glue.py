"""Existing reviewed loop and new gate integration, fake model/state only."""
import copy,sys,tempfile,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'context_training_loop'))
import training_loop
from test_contracts import fixture,backup,H
from state_codec import digest
from session_gate import OneSceneSession
class Fake:
    def input_digest(self,r):return digest(r)
    def validate_state(self,s,i):
        if s['optimizer']['step']!=s['cursor']['completed_updates']:raise ValueError('fake inconsistent step')
    def train(self,s,records,w,identity):
        assert len(records)==4 and w==.25
        n=copy.deepcopy(s);n['parameters']['w']+=1;n['optimizer']['step']+=1;n['optimizer']['m']['w']+=2;n['optimizer']['v']['w']+=3
        return n,{'batch_loss':.5,'pool_count':4}
class Glue(unittest.TestCase):
    def test_real_loop_durable_genesis_one_update_restore_with_fake_backend(self):
        scope,labels,records=fixture()
        policy={'records':{r['id']:{'split':'TRAIN','scene':scope['scene'],'condition':r['condition'],'teacher_domain':scope['teacher_domain'],'payload_sha256':r['payload_sha256']}for r in scope['records']},'final_ids':[],'teacher_domain':scope['teacher_domain'],'train_batches':[[r['id']for r in scope['records']]]}
        identity={'schema_sha256':training_loop.SCHEMA_SHA,'sources':{k:H for k in ('model','gru','objective','adamw','loop','adapter','checkpoint')},'policy_sha256':digest(policy),'shapes':{'w':[1]},'optimizer_config':{'fake':1.},'decay_keys':['w'],'conditions':scope['conditions']}
        initial={'parameters':{'w':0},'optimizer':{'step':0,'config':{'fake':1.},'m':{'w':0},'v':{'w':0}},'rng_state':{'fake':'unconsumed'},'cursor':{'completed_updates':0,'next_batch':0}}
        with tempfile.TemporaryDirectory()as directory:
            with training_loop.TrainingLoop(directory,identity,policy,Fake())as loop:
                genesis=loop.initialize(initial);g=OneSceneSession(loop,scope,digest(scope),H,0,H,'SYNTHETIC_QUALIFICATION_V1')
                with self.assertRaises(ValueError):g.update(records,labels,digest(labels))
                p=backup(loop,0);g.accept_verified_backup(p,digest(p));receipt=g.update(records,labels,digest(labels));final=copy.deepcopy(loop.state)
                self.assertEqual(g.status(),'BACKUP_AND_HEAD_REQUIRED');self.assertEqual(final['optimizer']['step'],1);self.assertEqual(final['rng_state'],initial['rng_state'])
                p=backup(loop,1);g.accept_verified_backup(p,digest(p));self.assertEqual(g.status(),'PILOT_COMPLETE')
            with training_loop.TrainingLoop(directory,identity,policy,Fake())as restored:
                pair={k:receipt[k]for k in ('issued_sha256','complete_sha256')};self.assertEqual(restored.restore_chain(genesis['genesis_sha256'],[pair]),final)
                g=OneSceneSession(restored,scope,digest(scope),H,2,H,'SYNTHETIC_QUALIFICATION_V1');self.assertEqual(g.status(),'BACKUP_AND_HEAD_REQUIRED')
    def test_real_namespace_cannot_be_selected_by_synthetic_payload(self):
        from test_contracts import FakeLoop
        scope,_,_=fixture()
        with self.assertRaises(ValueError):OneSceneSession(FakeLoop(scope),scope,digest(scope),H,0,H,'REAL_TRAIN_V1')
if __name__=='__main__':unittest.main()
