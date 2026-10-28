import tempfile,sys,unittest
from pathlib import Path
import pilot_adapter_v1 as a
import contract_numpy_v2 as c
import cache_store_numpy_v2 as cache
import journal_io_v2 as io

def contract():
 x={k:'a'*64 for k in c.HASH_FIELDS};x.update(schema='numpy-teacher-numeric-contract-v2',implementation='full_projection',formula=c.FORMULAS['full_projection'],precision='FP32',vocabulary_logits=8192,bos=2,eos=False,openblas_threads=2,affinity_width=2,runtime_contract_schema='NUMPY_STRICT_RUNTIME_CLOSURE_V1');return x
def admitted():return {'status':'STORAGE_ADMITTED','new_score_allowed':True}
class Tests(unittest.TestCase):
 def test_full_not_suffix(self):
  x=contract();a.full_contract(x);x['implementation']='suffix_projection';x['formula']=c.FORMULAS['suffix_projection']
  with self.assertRaises(ValueError):a.full_contract(x)
 def test_original_torch_contract_forbidden(self):
  x=contract();x['schema']='authoritative-A-numeric-v1'
  with self.assertRaises(ValueError):a.full_contract(x)
 def test_tiny_schedule_all_costs(self):
  m={k:{'input_id':h*64,'value':n} for k,h,n in [('forward_length','b',53),('prefix_length','c',46),('tail_length','d',10)]};s=a.tiny_schedule(m,'e'*64)
  self.assertEqual(s['score_calls'],8);self.assertEqual(s['repeat_calls'],3);self.assertEqual(s['sentinel_calls'],2);self.assertEqual(s['model_loads'],1);self.assertEqual(s['native_thread_getters'],2)
 def test_geometry_duplicate_deduplicated(self):
  m={k:{'input_id':'b'*64,'value':1} for k in ('forward_length','prefix_length','tail_length')};s=a.tiny_schedule(m,'e'*64);self.assertEqual(s['score_calls'],4)
 def test_order_and_repeat_bits(self):
  s=a.tiny_schedule({k:{'input_id':'b'*64,'value':1} for k in ('forward_length','prefix_length','tail_length')},'e'*64)
  r=[dict(input_id=o['input_id'],index=o['index'],value=-1.0) for o in s['operations']];self.assertTrue(a.check_repeat_bits(s,r,cache.fp32_bits(-1.0))['all_bits_equal']);r[2]['value']=-2.0
  with self.assertRaises(ValueError):a.check_repeat_bits(s,r,cache.fp32_bits(-1.0))
 def test_no_implicit_fp64_rounding(self):
  with self.assertRaises(ValueError):cache.fp32_bits(-.1)
 def setupio(self,root):
  spec=a.operation_spec(contract(),dict(cache_key='b'*64,input_id='c'*64),0);return io.Journal(root/'journal',spec,create=True),cache.CacheStore(root/'scores')
 def test_durable_issue_before_callback_and_complete(self):
  with tempfile.TemporaryDirectory() as td:
   j,s=self.setupio(Path(td))
   with j,s:
    def callback():
     self.assertEqual(j.classify()['classification']['status'],'UNKNOWN_ISSUED_WITHOUT_COMPLETION');return -1.0
    out=a.perform_one(j,s,callback,lambda:None,lambda:None,admitted)
    self.assertEqual(out['state']['issued'],1);self.assertEqual(out['state']['saved'],1);self.assertEqual(out['state']['hits'],0);self.assertFalse(out['production_entry'])
    with self.assertRaises(ValueError):a.perform_one(j,s,lambda:self.fail('repeat'),lambda:None,lambda:None,admitted)
 def test_callback_failure_is_unknown_no_retry(self):
  with tempfile.TemporaryDirectory() as td:
   j,s=self.setupio(Path(td));calls=[]
   with j,s:
    def fail():calls.append(1);raise RuntimeError('synthetic loss')
    with self.assertRaises(RuntimeError):a.perform_one(j,s,fail,lambda:None,lambda:None,admitted)
    self.assertEqual(j.classify()['classification']['unknown_score_upper_bound'],1)
    with self.assertRaises(ValueError):a.perform_one(j,s,fail,lambda:None,lambda:None,admitted)
    self.assertEqual(len(calls),1)
 def test_postguard_failure_retains_completed_not_promoted(self):
  with tempfile.TemporaryDirectory() as td:
   j,s=self.setupio(Path(td))
   with j,s:
    def fail():raise RuntimeError('guard failed')
    with self.assertRaises(RuntimeError):a.perform_one(j,s,lambda:-1.0,lambda:None,fail,admitted)
    self.assertEqual(j.classify()['classification']['state'],'COMPLETED');self.assertEqual(list((Path(td)/'scores').iterdir()),[])
 def test_pre_guard_failure_no_callback(self):
  with tempfile.TemporaryDirectory() as td:
   j,s=self.setupio(Path(td))
   with j,s:
    def fail():raise RuntimeError('pre guard')
    with self.assertRaises(RuntimeError):a.perform_one(j,s,lambda:self.fail('no score'),fail,lambda:None,admitted)
    self.assertEqual(j.classify()['classification']['issued'],0)
 def test_train_only_preserves_all_candidates(self):
  w={'occurrences':[{'condition_id':'t','split':'train','candidates':[{'supported':False},{'supported':True}]},{'condition_id':'d','split':'student_dev','candidates':[]}]}
  self.assertEqual(a.train_only_occurrences(w,['t']),[w['occurrences'][0]])
  with self.assertRaises(ValueError):a.train_only_occurrences(w,['d'])
 def test_low_storage_stops_before_issued(self):
  with tempfile.TemporaryDirectory() as td:
   j,s=self.setupio(Path(td))
   with j,s:
    with self.assertRaises(ValueError):a.perform_one(j,s,lambda:self.fail('no score'),lambda:None,lambda:None,lambda:{'status':'STOP_LOW_STORAGE_BEFORE_NEW_ISSUED','new_score_allowed':False})
    self.assertEqual(j.classify()['classification']['issued'],0)
 def test_no_numpy(self):
  for n in ('numpy','torch','_ctypes'):self.assertNotIn(n,sys.modules)
if __name__=='__main__':unittest.main()
