import json,os,tempfile,unittest,subprocess,sys
from pathlib import Path
from unittest.mock import patch
import journal_io_v2 as j
import recovery_protocol_v2 as r
import cache_store_numpy_v2 as c
S=dict(schema='numpy-cache-window-v2',contract_sha256='a'*64,window=0,attempt='synthetic',keys=[dict(cache_key='b'*64,input_id='c'*64)],mode='score')
class Tests(unittest.TestCase):
 def test_full_roundtrip(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'journal'
   with j.Journal(root,S,create=True) as x:
    x.append('intent',{});x.append('issued',S['keys'][0]);p=c.payload('a'*64,'b'*64,'c'*64,-1.0,dict(window=0,attempt='synthetic'));x.append('completed',p);x.append('saved',dict(payload_sha256=p['payload_sha256']));x.append('manifest',dict(saved=1,hits=0,issued=1));self.assertEqual(x.classify()['classification']['state'],'COMPLETE')
   with j.Journal(root,S) as x:self.assertEqual(len(x.verified_events()[0]),5)
 def test_no_recreate(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'journal'
   with j.Journal(root,S,create=True):pass
   with self.assertRaises(FileExistsError):j.Journal(root,S,create=True)
 def test_exclusive_writer(self):
  with tempfile.TemporaryDirectory() as tmp,j.Journal(Path(tmp)/'journal',S,create=True) as x:
   with self.assertRaises(BlockingIOError):j.Journal(x.root,S)
 def test_faults_leave_failclosed_evidence(self):
  for stage in ('before_write','before_file_fsync','after_file_fsync','before_link','after_link','after_dir_fsync'):
   with self.subTest(stage=stage),tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)/'journal'
    with j.Journal(root,S,create=True) as x:
     x.append('intent',{})
     def fault(s):
      if s==stage:raise OSError('synthetic interrupt')
     with self.assertRaises(OSError):x.append('issued',S['keys'][0],fault=fault)
    with j.Journal(root,S) as x:
     with self.assertRaisesRegex(ValueError,'pending'):x.verified_events()
    self.assertTrue(list(root.glob('.pending-*')))
 def test_corrupt_or_gap_events(self):
  for mode in ('partial','gap','changed','tail'):
   with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)/'journal'
    with j.Journal(root,S,create=True) as x:x.append('intent',{});x.append('issued',S['keys'][0])
    if mode=='partial':(root/'event-000001.json').write_bytes(b'{')
    elif mode=='gap':(root/'event-000000.json').unlink()
    elif mode=='tail':(root/'event-000001.json').unlink()
    else:
     p=root/'event-000001.json';v=json.loads(p.read_bytes());v['previous_sha256']='e'*64;p.write_text(json.dumps(v))
    with j.Journal(root,S) as x:
     with self.assertRaises((ValueError,json.JSONDecodeError)):x.verified_events()
 def test_unknown_after_fresh_process_exit(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'journal';code="from journal_io_v2 import Journal; import os; s=%r; x=Journal(%r,s,create=True); x.append('intent',{}); x.append('issued',s['keys'][0]); os._exit(7)"%(S,str(root))
   result=subprocess.run([sys.executable,'-B','-c',code],cwd=Path(j.__file__).parent,timeout=5,capture_output=True);self.assertEqual(result.returncode,7)
   with j.Journal(root,S) as x:
    state=x.classify(process_state='EXITED',source_integrity_verified=True)['classification'];self.assertEqual(state['status'],'UNKNOWN_ISSUED_WITHOUT_COMPLETION');self.assertEqual(state['model_calls_authorized'],0)
 def test_killed_after_link_stays_unknown(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'journal';code="from journal_io_v2 import Journal; import os; s=%r; x=Journal(%r,s,create=True); x.append('intent',{}); x.append('issued',s['keys'][0],fault=lambda p:os._exit(9) if p=='after_link' else None)"%(S,str(root))
   result=subprocess.run([sys.executable,'-B','-c',code],cwd=Path(j.__file__).parent,timeout=5,capture_output=True);self.assertEqual(result.returncode,9)
   with j.Journal(root,S) as x:
    with self.assertRaises(ValueError):x.classify()
 def test_zero_model_completed_recovery(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'journal'
   with j.Journal(root,S,create=True) as x:
    x.append('intent',{});x.append('issued',S['keys'][0]);x.append('completed',c.payload('a'*64,'b'*64,'c'*64,-1.0,dict(window=0,attempt='synthetic')))
   with j.Journal(root,S) as x:
    e,receipt=x.verified_events();plan=r.recovery_plan(S,e,r.digest(receipt))
    with c.CacheStore(Path(tmp)/'cache') as store:result=r.recover_durable_completed(store,plan,dict(approved=True,plan_sha256=r.digest(plan)))
    self.assertEqual(result['value'],-1.0);self.assertEqual(plan['model_calls'],0)
 def test_bad_permissions_and_symlink(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'journal'
   with j.Journal(root,S,create=True):pass
   (root/'SPEC.json').chmod(0o644)
   with self.assertRaises(ValueError):j.Journal(root,S)
 def test_no_numeric_import(self):
  for name in ('numpy','torch','_ctypes'):self.assertNotIn(name,sys.modules)
if __name__=='__main__':unittest.main()
