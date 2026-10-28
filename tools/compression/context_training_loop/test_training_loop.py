import copy
import hashlib
import json
import random
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from checkpoint_store import digest,read_sealed
from training_loop import TrainingLoop,SCHEMA_SHA,validate_state
from numpy_adapter import validate_records

class Fake:
    def __init__(self): self.calls=[];self.fail=False
    def validate_state(self,state,identity):
        if set(state['parameters'])!={'p'}:raise ValueError('params')
        if set(state['optimizer'])!={'step','config','m','v'}:raise ValueError('optimizer')
        if set(state['optimizer']['m'])!={'p'} or set(state['optimizer']['v'])!={'p'}:raise ValueError('moments')
    def input_digest(self,record):return digest(record)
    def train(self,state,records,weight,identity):
        self.calls.append(('train',len(records),weight))
        if self.fail:raise RuntimeError('injected failure')
        state['parameters']['p']+=1;state['optimizer']['step']+=1
        state['optimizer']['m']['p']+=2;state['optimizer']['v']['p']+=3
        return state,{'batch_loss':4*weight,'pool_count':len(records)}
    def evaluate(self,state,records,identity):
        self.calls.append(('dev',len(records)));state['parameters']['p']=999
        return [0]*len(records)

def fixture():
    policy={'records':{},'final_ids':['final-10000'],'teacher_domain':'teacher-seen-domain-only','train_batches':[["sa","sb","sc","sd"]]*2}
    for scene,split in [('s','TRAIN'),('t','TRAIN'),('d','DEV')]:
        for condition in 'abcd':policy['records'][scene+condition]={'scene':scene,'split':split,'condition':condition,'teacher_domain':policy['teacher_domain'],'payload_sha256':digest({'id':scene+condition,'context':[],'candidates':[[],[]],'teacher_scores':[0,0],'gold_index':None} if split=='TRAIN' else {'id':scene+condition,'context':[],'candidates':[]})}
    identity={'schema_sha256':SCHEMA_SHA,'sources':{x:'a'*64 for x in ['model','gru','objective','adamw','loop','adapter','checkpoint']},'policy_sha256':digest(policy),'shapes':{'p':[1]},'optimizer_config':{},'decay_keys':[],'conditions':list('abcd')}
    state={'parameters':{'p':1},'optimizer':{'step':0,'config':{},'m':{'p':0},'v':{'p':0}},'rng_state':json.loads(json.dumps(random.Random(0).getstate())),'cursor':{'completed_updates':0,'next_batch':0}}
    records=[{'id':'s'+c,'context':[],'candidates':[[],[]],'teacher_scores':[0,0],'gold_index':None} for c in 'abcd']
    return identity,policy,state,records

def file_sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

class LoopTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'run';self.i,self.p,self.s,self.r=fixture();self.b=Fake()
        self.loop=TrainingLoop(self.path,self.i,self.p,self.b);self.addCleanup(self.loop.close)
        self.initial=self.loop.initialize(self.s);self.gen=file_sha(self.path/'genesis.json')
    def reopen(self):
        self.loop.close();self.loop=TrainingLoop(self.path,self.i,self.p,self.b);self.addCleanup(self.loop.close)
    def test_update_resume_all_state(self):
        receipt=self.loop.update(self.r);expected=copy.deepcopy(self.loop.state)
        self.reopen();actual=self.loop.restore_chain(self.gen,[{k:receipt[k] for k in ('issued_sha256','complete_sha256')}])
        self.assertEqual(digest(actual),digest(expected));self.assertEqual(self.b.calls,[('train',4,0.25)])
        self.assertEqual(actual['optimizer']['m'],{'p':2});self.assertEqual(actual['cursor']['next_batch'],1)
    def test_two_scenes_weight(self):
        rows=self.r+[{**r,'id':'t'+r['id'][1]} for r in self.r]
        self.loop.close();self.p['train_batches']=[[r['id'] for r in rows]];self.i['policy_sha256']=digest(self.p)
        self.loop=TrainingLoop(Path(self.tmp.name)/'multi',self.i,self.p,self.b);self.addCleanup(self.loop.close);self.loop.initialize(self.s)
        self.loop.update(rows);self.assertEqual(self.b.calls,[('train',8,0.125)])
    def test_dev_forward_only_no_state_mutation(self):
        before=digest(self.loop.state);self.loop.evaluate_dev([{'id':'da','context':[],'candidates':[]}])
        self.assertEqual(digest(self.loop.state),before);self.assertEqual(self.b.calls,[('dev',1)])
    def test_dev_training_denied(self):
        for bad in ('da','final-10000','unknown'):
            rows=copy.deepcopy(self.r);rows[0]['id']=bad
            with self.assertRaises(ValueError):self.loop.update(rows)
        self.assertEqual(self.b.calls,[])
    def test_incomplete_duplicate_conditions_denied(self):
        for rows in (self.r[:3],self.r+[self.r[0]]):
            with self.assertRaises(ValueError):self.loop.update(rows)
        self.assertEqual(self.b.calls,[])
    def test_dev_targets_denied(self):
        with self.assertRaises(ValueError):self.loop.evaluate_dev([{**self.r[0],'id':'da'}])
        self.assertEqual(self.b.calls,[])
    def test_failed_issued_blocks_live_and_restore(self):
        self.b.fail=True
        with self.assertRaises(RuntimeError):self.loop.update(self.r)
        self.assertTrue(self.loop.marker(1,'issued').exists())
        with self.assertRaises(ValueError):self.loop.update(self.r)
        with self.assertRaises(ValueError):self.loop.evaluate_dev([{'id':'da','context':[],'candidates':[]}])
        self.reopen()
        with self.assertRaises(ValueError):self.loop.restore(self.gen)
        self.assertEqual(len(self.b.calls),1)
    def test_checkpoint_exists_without_complete_is_unknown(self):
        import training_loop
        original=training_loop.save_new
        def fail(path,payload):
            if str(path).endswith('.complete.json'):raise OSError('injected before complete')
            return original(path,payload)
        with patch.object(training_loop,'save_new',fail):
            with self.assertRaises(OSError):self.loop.update(self.r)
        self.assertTrue(self.loop.checkpoint(1).exists());self.reopen()
        with self.assertRaises(ValueError):self.loop.restore(self.gen)
    def test_wrong_identity_and_partial_chain_denied(self):
        r=self.loop.update(self.r);self.reopen()
        with self.assertRaises(ValueError):self.loop.restore(self.gen)
        bad={k:r[k] for k in ('issued_sha256','complete_sha256')};bad['complete_sha256']='0'*64
        with self.assertRaises(ValueError):self.loop.restore_chain(self.gen,[bad])
    def test_concurrent_owner_denied(self):
        with self.assertRaises(BlockingIOError):TrainingLoop(self.path,self.i,self.p,Fake())
    def test_mutated_policy_denied(self):
        p=copy.deepcopy(self.p);p['records']['sa']['split']='DEV'
        with self.assertRaises(ValueError):TrainingLoop(self.path,self.i,p,Fake())
    def test_bad_cursor_and_missing_moments(self):
        state=copy.deepcopy(self.s);state['cursor']['next_batch']=1
        with self.assertRaises(ValueError):validate_state(state,self.i,self.b)
        state=copy.deepcopy(self.s);del state['optimizer']['m']['p']
        with self.assertRaises(ValueError):validate_state(state,self.i,self.b)
    def test_genesis_receipt(self):
        self.assertEqual(self.initial,{'genesis_sha256':self.gen,'checkpoint_sha256':file_sha(self.loop.checkpoint(0))})
    def test_nonempty_chain_checks_genesis_checkpoint(self):
        receipt=self.loop.update(self.r);self.loop.checkpoint(0).write_bytes(b'corrupted')
        self.reopen()
        with self.assertRaises(ValueError):self.loop.restore_chain(self.gen,[{k:receipt[k] for k in ('issued_sha256','complete_sha256')}])
    def test_same_id_changed_payload_denied(self):
        rows=copy.deepcopy(self.r);rows[0]['teacher_scores']=[999,0]
        with self.assertRaises(ValueError):self.loop.update(rows)
        self.assertEqual(self.b.calls,[])
    def test_dev_receipt_chain(self):
        result=self.loop.evaluate_dev([{'id':'da','context':[],'candidates':[]}])
        before=digest(self.loop.state);self.reopen();self.loop.restore_chain(self.gen,[],[result['receipt']])
        self.assertEqual(before,digest(self.loop.state));self.assertEqual(self.loop.dev_count,1)
    def test_dev_failure_unknown_no_continuation(self):
        with patch.object(self.b,'evaluate',side_effect=RuntimeError('injected DEV failure')):
            with self.assertRaises(RuntimeError):self.loop.evaluate_dev([{'id':'da','context':[],'candidates':[]}])
        with self.assertRaises(ValueError):self.loop.update(self.r)
        self.reopen()
        with self.assertRaises(ValueError):self.loop.restore(self.gen)
    def test_schedule_order_bound(self):
        with self.assertRaises(ValueError):self.loop.update(list(reversed(self.r)))
        self.assertEqual(self.b.calls,[])
    def test_array_schema_pure_stdlib(self):
        import struct
        r={'p':{'shape':[1],'dtype':'<f8','hex':struct.pack('<d',1.0).hex()}}
        validate_records(r,{'p':[1]});r['p']['hex']=struct.pack('<d',float('nan')).hex()
        with self.assertRaises(ValueError):validate_records(r,{'p':[1]})
        r['p']['hex']=struct.pack('<d',-1.0).hex()
        with self.assertRaises(ValueError):validate_records(r,{'p':[1]},True)

if __name__=='__main__':unittest.main()
