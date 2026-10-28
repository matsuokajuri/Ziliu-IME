import copy,json,os,tempfile,unittest,subprocess,sys
from pathlib import Path
from unittest.mock import patch
import storage_admission_v2 as d
import evidence_store_v3 as e
I='a'*64;S='b'*64
class Tests(unittest.TestCase):
 def snapshot(self,phase='score-0-before',maptext='1000-2000 r-xp 0 00:00 1 /synthetic/library.so\n',newline=False):
  obj=dict(phase=phase,file_sha256={'synthetic-library':'c'*64},frontend_metadata=[{'module':'synthetic','origin':'source'}],file_backed_executables=['/synthetic/library.so'],kernel_unhashed=['vdso'])
  for side in ('observed','after'):obj[side+'_maps_private']=maptext;obj[side+'_maps_sha256']=e.sha(maptext.encode())
  return e.canonical(obj)+(b'\n' if newline else b'')
 def test_budget_exact_unit_and_reserve(self):
  c=d.DEFAULT;v=d.budget(c,77489,597,4096);self.assertEqual(v['allocated_bytes'],14343442432);self.assertEqual(d.rounded(4097,4096),8192)
  obs=dict(available_bytes=v['allocated_bytes']+c['safety_reserve_bytes'],available_inodes=v['inodes']+c['inode_reserve'],allocation_unit=4096)
  self.assertTrue(d.admit(c,77489,597,obs)['new_score_allowed']);obs['available_bytes']-=1;self.assertFalse(d.admit(c,77489,597,obs)['new_score_allowed'])
 def test_inode_gate_before_new_issued(self):
  obs=dict(available_bytes=10**15,available_inodes=0,allocation_unit=4096);self.assertFalse(d.admit(d.DEFAULT,1,0,obs)['new_score_allowed'])
 def test_caps_typed_and_pinned(self):
  c=dict(d.DEFAULT);c['map_blob_max_bytes']=True
  with self.assertRaises(ValueError):d.validate(c)
  c=dict(d.DEFAULT);h=d.validate(c);c['map_blob_max_bytes']+=1;self.assertNotEqual(d.validate(c),h)
 def test_lossless_roundtrip_dedup_and_bindings(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'store';a=self.snapshot();b=self.snapshot('score-0-after',newline=True)
   with e.EvidenceStore(root,I,S,d.DEFAULT,create=True) as st:
    st.put_checkpoint(a,0,'score-0-before');st.put_checkpoint(b,1,'score-0-after');self.assertEqual(st.read_checkpoint(0,'score-0-before'),a);self.assertEqual(st.read_checkpoint(1,'score-0-after'),b);self.assertEqual(len(list(root.glob('map-*'))),1);self.assertEqual(len(list(root.glob('closure-*'))),1)
    refs=[dict(sequence=0,phase='score-0-before',original_sha256=e.sha(a)),dict(sequence=1,phase='score-0-after',original_sha256=e.sha(b))];self.assertEqual(len(st.verify_all(refs)['checkpoints']),2)
   with e.EvidenceStore(root,I,S,d.DEFAULT) as st:self.assertEqual(st.read_checkpoint(0,'score-0-before'),a)
   with self.assertRaises(ValueError):e.EvidenceStore(root,'f'*64,S,d.DEFAULT)
 def test_wrong_sequence_phase(self):
  with tempfile.TemporaryDirectory() as tmp,e.EvidenceStore(Path(tmp)/'s',I,S,d.DEFAULT,create=True) as st:
   a=self.snapshot()
   with self.assertRaises(ValueError):st.put_checkpoint(a,1,'score-0-before')
   with self.assertRaises(ValueError):st.put_checkpoint(a,0,'wrong')
   st.put_checkpoint(a,0,'score-0-before')
   with self.assertRaises(ValueError):st.read_checkpoint(0,'wrong')
 def test_source_closure_change_rejected(self):
  with tempfile.TemporaryDirectory() as tmp,e.EvidenceStore(Path(tmp)/'s',I,S,d.DEFAULT,create=True) as st:
   st.put_checkpoint(self.snapshot(),0,'score-0-before');obj=json.loads(self.snapshot('score-0-after'));obj['file_sha256']['synthetic-library']='d'*64
   with self.assertRaises(ValueError):st.put_checkpoint(e.canonical(obj),1,'score-0-after')
 def test_noncanonical_duplicate_rejected(self):
  with tempfile.TemporaryDirectory() as tmp,e.EvidenceStore(Path(tmp)/'s',I,S,d.DEFAULT,create=True) as st:
   with self.assertRaises(ValueError):st.put_checkpoint(json.dumps(json.loads(self.snapshot()),indent=2).encode(),0,'score-0-before')
   with self.assertRaises(ValueError):e.loads(b'{"a":1,"a":2}')
 def test_cap_plus_one_never_truncates(self):
  c=dict(d.DEFAULT);c['map_blob_max_bytes']=4
  with tempfile.TemporaryDirectory() as tmp,e.EvidenceStore(Path(tmp)/'s',I,S,c,create=True) as st:
   with self.assertRaises(ValueError):st.put_checkpoint(self.snapshot(maptext='12345'),0,'score-0-before')
   self.assertEqual(set(os.listdir(st.fd)),{'SPEC.json','LOCK'})
 def test_map_corruption_never_miss_rebuild(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'s'
   with e.EvidenceStore(root,I,S,d.DEFAULT,create=True) as st:
    a=self.snapshot();st.put_checkpoint(a,0,'score-0-before');blob=next(root.glob('map-*'));blob.write_bytes(b'corrupt')
    with self.assertRaises(ValueError):st.read_checkpoint(0,'score-0-before')
    with self.assertRaises(ValueError):st.put_checkpoint(self.snapshot('score-0-after'),1,'score-0-after')
    self.assertEqual(blob.read_bytes(),b'corrupt')
 def test_missing_map_or_tampered_reference(self):
  for mode in ('missing','binding','objecthash'):
   with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)/'s'
    with e.EvidenceStore(root,I,S,d.DEFAULT,create=True) as st:
     st.put_checkpoint(self.snapshot(),0,'score-0-before')
     if mode=='missing':next(root.glob('map-*')).unlink()
     else:
      path=root/'checkpoint-000000.json';r=json.loads(path.read_bytes())
      if mode=='binding':r['binding']['sequence']=1
      else:r['original_canonical_sha256']='d'*64
      path.write_bytes(e.canonical(r))
     with self.assertRaises((ValueError,FileNotFoundError)):st.read_checkpoint(0,'score-0-before')
 def test_faults_keep_evidence_and_failclosed(self):
  for stage in ('closure:before_write','closure:after_link','map:after_file_fsync','before_reference','reference:before_link','reference:after_link'):
   with self.subTest(stage=stage),tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)/'s'
    with e.EvidenceStore(root,I,S,d.DEFAULT,create=True) as st:
     def fault(s):
      if s==stage:raise OSError('synthetic ENOSPC/interruption')
     with self.assertRaises(OSError):st.put_checkpoint(self.snapshot(),0,'score-0-before',fault=fault)
    self.assertGreater(len(list(root.iterdir())),2)
    with e.EvidenceStore(root,I,S,d.DEFAULT) as st:
     with self.assertRaises(ValueError):st.put_checkpoint(self.snapshot(),0,'score-0-before')
 def test_hash_collision_different_map_bytes(self):
  with tempfile.TemporaryDirectory() as tmp,e.EvidenceStore(Path(tmp)/'s',I,S,d.DEFAULT,create=True) as st:
   st._publish('map-'+'f'*64+'.blob',b'one',100)
   with self.assertRaises(ValueError):st._publish('map-'+'f'*64+'.blob',b'two',100)
 def test_new_process_exact_reconstruction(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'s';a=self.snapshot()
   with e.EvidenceStore(root,I,S,d.DEFAULT,create=True) as st:st.put_checkpoint(a,0,'score-0-before')
   code="import evidence_store_v3 as e,storage_admission_v2 as d; s=e.EvidenceStore(%r,%r,%r,d.DEFAULT); assert e.sha(s.read_checkpoint(0,'score-0-before'))==%r; s.close()"%(str(root),I,S,e.sha(a));r=subprocess.run([sys.executable,'-B','-c',code],cwd=Path(e.__file__).parent,timeout=5,capture_output=True);self.assertEqual(r.returncode,0,r.stderr)
 def test_lowdisk_gate_no_side_effect(self):
  v=d.admit(d.DEFAULT,1,0,dict(available_bytes=0,available_inodes=0,allocation_unit=4096));self.assertEqual(v['status'],'STOP_LOW_STORAGE_BEFORE_NEW_ISSUED');self.assertFalse(v['space_actually_reserved'])
 def test_reference_cap_larger_than_other_caps(self):
  c=dict(d.DEFAULT);c.update(closure_max_bytes=4096,map_blob_max_bytes=4096,reference_max_bytes=65536)
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'s';raw=self.snapshot();phase='score-0-before'
   with e.EvidenceStore(root,I,S,c,create=True) as st:
    st.put_checkpoint(raw,0,phase)
    # Synthetic valid JSON whitespace envelope exercises the generic read cap.
    # Current canonical reference writer does not naturally produce a 16 KiB ref.
    ref=root/'checkpoint-000000.json';padded=ref.read_bytes()+b' ' * 20000
    self.assertLess(len(padded),c['reference_max_bytes']);ref.write_bytes(padded)
    result=st.verify_all([dict(sequence=0,phase=phase,original_sha256=e.sha(raw))])
    self.assertEqual(result['file_sha256'][ref.name],e.sha(padded))
    self.assertEqual(st.read_checkpoint(0,phase),raw)
    ref.write_bytes(padded+b' ' * c['reference_max_bytes'])
    with self.assertRaises(ValueError):st.verify_all([dict(sequence=0,phase=phase,original_sha256=e.sha(raw))])
 def test_spec_bound_even_with_small_other_caps(self):
  c=dict(d.DEFAULT);c.update(closure_max_bytes=1024,map_blob_max_bytes=128,reference_max_bytes=1200)
  with tempfile.TemporaryDirectory() as tmp,e.EvidenceStore(Path(tmp)/'s',I,S,c,create=True) as st:
   raw=self.snapshot();st.put_checkpoint(raw,0,'score-0-before')
   result=st.verify_all([dict(sequence=0,phase='score-0-before',original_sha256=e.sha(raw))])
   self.assertIn('SPEC.json',result['file_sha256'])
 def test_no_backend_or_remote(self):
  for n in ('numpy','torch','_ctypes','requests'):self.assertNotIn(n,sys.modules)
if __name__=='__main__':unittest.main()
