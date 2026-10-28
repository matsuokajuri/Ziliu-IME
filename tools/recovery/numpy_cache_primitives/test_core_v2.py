import copy,json,os,tempfile,unittest,hashlib,sys
from pathlib import Path
from unittest.mock import patch
import contract_numpy_v2 as c
import recovery_protocol_v2 as r
import cache_store_numpy_v2 as st
C='a'*64;K='b'*64;I='c'*64
class Contract(unittest.TestCase):
 def fixture(self):
  x={k:'a'*64 for k in c.HASH_FIELDS};x.update(schema='numpy-teacher-numeric-contract-v2',implementation='full_projection',formula=c.FORMULAS['full_projection'],precision='FP32',vocabulary_logits=8192,bos=2,eos=False,openblas_threads=2,affinity_width=2,runtime_contract_schema='NUMPY_STRICT_RUNTIME_CLOSURE_V1');return x
 def inputs(self):return [dict(input_id=c.input_identity([2,3],[4]),prefix=[2,3],tail=[4]),dict(input_id=c.input_identity([2],[5]),prefix=[2],tail=[5])]
 def test_valid_does_not_mutate(self):
  x=self.fixture();rows=self.inputs();before=copy.deepcopy((x,rows));v=c.bind_inputs(rows,x);self.assertEqual((x,rows),before);self.assertEqual(len(v['bindings']),2);self.assertFalse(v['real_scoring_authorized'])
 def test_source_change_separates_keys(self):
  x=self.fixture();a=c.bind_inputs(self.inputs(),x);x['scorer_sha256']='d'*64;b=c.bind_inputs(self.inputs(),x);self.assertNotEqual(a['bindings'],b['bindings'])
 def test_implementation_separates_keys(self):
  x=self.fixture();a=c.bind_inputs(self.inputs(),x);x.update(implementation='suffix_projection',formula=c.FORMULAS['suffix_projection']);self.assertNotEqual(a['bindings'],c.bind_inputs(self.inputs(),x)['bindings'])
 def test_a_thread_dtype_downgrade_rejected(self):
  for key,value in [('schema','authoritative-A-numeric-v1'),('precision','FP16'),('openblas_threads',1),('affinity_width',True),('eos',0),('formula','wrong'),('runtime_identity_sha256','0'*64)]:
   x=self.fixture();x[key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):c.validate_contract(x)
 def test_extra_fields_rejected(self):
  x=self.fixture();x['inter_threads']=1
  with self.assertRaises(ValueError):c.validate_contract(x)
 def test_numeric_key_collision(self):
  with self.assertRaisesRegex(ValueError,'collision'):c.bind_inputs(self.inputs(),self.fixture(),hasher=lambda b:'f'*64)
 def test_wrong_input_duplicate(self):
  a=self.inputs()
  with self.assertRaises(ValueError):c.bind_inputs(a+a,self.fixture())
  a[0]['input_id']='d'*64
  with self.assertRaises(ValueError):c.bind_inputs(a,self.fixture())
 def test_invalid_token_geometry(self):
  for p,t in [([3],[4]),([2],[True]),([2],[2]),([2]*64,[4]),([2],[])]:
   with self.assertRaises(ValueError):c.input_identity(p,t)
 def test_no_numeric_or_remote_import(self):
  for n in ('numpy','torch','_ctypes','requests'):self.assertNotIn(n,sys.modules)
class Recovery(unittest.TestCase):
 def fixture(self):
  s=dict(schema='numpy-cache-window-v2',contract_sha256=C,window=0,attempt='synthetic',keys=[dict(cache_key=K,input_id=I)],mode='score');e=[r.event(s,[],'intent',{})];return s,e
 def add(self,s,e,k,d):e.append(r.event(s,e,k,d))
 def complete(self):
  s,e=self.fixture();self.add(s,e,'issued',s['keys'][0]);p=st.payload(C,K,I,-1.5,dict(window=0,attempt='synthetic'));self.add(s,e,'completed',p);return s,e,p
 def test_no_start_requires_confirmed_and_sources(self):
  s,e=self.fixture();self.assertEqual(r.inspect(s,e)['status'],'INCOMPLETE_REQUIRES_RECONCILIATION')
  self.assertEqual(r.inspect(s,e,process_state='NOT_LAUNCHED',source_integrity_verified=True)['status'],'NUMERIC_NOT_STARTED_NEW_ATTEMPT_REQUIRES_REVIEW')
 def test_unknown_issued_never_retries(self):
  s,e=self.fixture();self.add(s,e,'issued',s['keys'][0]);v=r.inspect(s,e,process_state='EXITED',source_integrity_verified=True);self.assertEqual(v['unknown_score_upper_bound'],1);self.assertFalse(v['automatic_retry']);self.assertEqual(v['model_calls_authorized'],0)
  with self.assertRaises(ValueError):r.recovery_plan(s,e,'f'*64)
 def test_no_second_issue_or_skip(self):
  s,e=self.fixture();self.add(s,e,'issued',s['keys'][0]);self.add(s,e,'issued',s['keys'][0])
  with self.assertRaises(ValueError):r.inspect(s,e)
 def test_cache_only_cannot_issue(self):
  s,e=self.fixture();s['mode']='cache_only';e=[r.event(s,[],'intent',{})];self.add(s,e,'issued',s['keys'][0])
  with self.assertRaises(ValueError):r.inspect(s,e)
 def test_torn_chain_or_modified_event_rejected(self):
  s,e,p=self.complete();e[1]['data']['cache_key']='e'*64
  with self.assertRaises(ValueError):r.inspect(s,e)
 def test_completion_identity_mismatch(self):
  s,e,p=self.complete();e[-1]['data']=st.payload(C,K,I,-1.5,dict(window=0,attempt='other'))
  with self.assertRaises(ValueError):r.inspect(s,e)
 def test_complete_needs_saved_manifest(self):
  s,e,p=self.complete();self.assertEqual(r.inspect(s,e)['state'],'COMPLETED');self.add(s,e,'saved',{'payload_sha256':p['payload_sha256']});self.assertEqual(r.inspect(s,e)['status'],'INCOMPLETE_REQUIRES_RECONCILIATION');self.add(s,e,'manifest',dict(saved=1,hits=0,issued=1));self.assertEqual(r.inspect(s,e)['status'],'COMPLETE_REQUIRES_FRESH_CACHE_ONLY_VERIFICATION')
 def test_manifest_bool_counts_rejected(self):
  s,e=self.fixture();self.add(s,e,'hit',st.payload(C,K,I,-1.5,dict(window=0,attempt='other')));self.add(s,e,'manifest',dict(saved=False,hits=1,issued=0))
  with self.assertRaises(ValueError):r.inspect(s,e)
 def test_recover_completed_zero_model_and_conflict_refusal(self):
  s,e,p=self.complete();plan=r.recovery_plan(s,e,'f'*64);review=dict(approved=True,plan_sha256=r.digest(plan))
  with tempfile.TemporaryDirectory() as tmp,st.CacheStore(Path(tmp)/'cache') as store:
   self.assertEqual(r.recover_durable_completed(store,plan,review),p)
   self.assertEqual(r.recover_durable_completed(store,plan,review),p)
   bad=copy.deepcopy(plan);bad['payload']=st.payload(C,K,I,-2.0,p['origin'])
   with self.assertRaises(ValueError):r.recover_durable_completed(store,bad,dict(approved=True,plan_sha256=r.digest(bad)))
 def test_recovery_requires_exact_review(self):
  s,e,p=self.complete();plan=r.recovery_plan(s,e,'f'*64)
  with tempfile.TemporaryDirectory() as tmp,st.CacheStore(Path(tmp)/'cache') as store:
   for approval in (dict(approved=1,plan_sha256=r.digest(plan)),dict(approved=True,plan_sha256='e'*64)):
    with self.assertRaises(ValueError):r.recover_durable_completed(store,plan,approval)
 def test_cache_fsync_failure_reconciles_not_retry(self):
  with tempfile.TemporaryDirectory() as tmp,st.CacheStore(Path(tmp)/'cache') as store:
   with patch.object(st.os,'fsync',side_effect=OSError('synthetic fsync failure')):
    with self.assertRaises(OSError):store.publish(C,K,I,-1.0,dict(window=0,attempt='synthetic'))
   with self.assertRaises(FileNotFoundError):store.read_cache_only(C,K,I)
 def test_temp_corruption_prevents_publication(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'cache'
   with st.CacheStore(root) as store:
    def fault(point):
     if point=='after_temp_write':next(root.glob('.pending-*')).write_bytes(b'partial')
    with self.assertRaises(ValueError):store.publish(C,K,I,-1.0,dict(window=0,attempt='synthetic'),fault=fault)
    self.assertFalse((root/(K+'.json')).exists())
if __name__=='__main__':unittest.main()
