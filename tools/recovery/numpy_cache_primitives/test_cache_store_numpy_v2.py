import copy,importlib.util,json,math,os,pathlib,struct,tempfile,unittest
p=pathlib.Path(__file__).with_name('cache_store_numpy_v2.py');s=importlib.util.spec_from_file_location('store',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
C='a'*64;K='b'*64;I='c'*64;O={'window':0,'attempt':'test-0'}
class T(unittest.TestCase):
 def test_fp32_exact_and_negative_zero(self):
  self.assertEqual(m.fp32_bits(-0.0),'00000080');self.assertEqual(m.fp32_bits(0.0),'00000000');self.assertEqual(m.fp32_bits(-1.5),'0000c0bf');v=struct.unpack('<f',bytes.fromhex('01000080'))[0];self.assertEqual(m.fp32_bits(v),'01000080')
 def test_bad_float_types_rounding(self):
  for v in [True,1,1.0,0.1,float('nan'),float('inf'),-float('inf'),1e100]:
   with self.assertRaises(ValueError):m.fp32_bits(v)
 def test_exact_payload_validation(self):
  p=m.payload(C,K,I,-1.5,O);self.assertEqual(m.validate(p,C,K,I),p)
  for key,value in [('value',-1),('fp32_le_hex','00000000'),('input_id','d'*64),('schema','wrong'),('payload_sha256','f'*64)]:
   q=copy.deepcopy(p);q[key]=value
   with self.assertRaises(ValueError):m.validate(q,C,K,I)
 def test_origin_strict(self):
  for origin in [{'window':True,'attempt':'x'},{'window':0,'attempt':'../x'},{'window':-1,'attempt':'x'}]:
   with self.assertRaises(ValueError):m.payload(C,K,I,-1.0,origin)
 def test_private_publish_read_and_no_overwrite(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)/'cache'
   with m.CacheStore(root) as st:
    p=st.publish(C,K,I,-1.5,O);path=root/(K+'.json');before=path.read_bytes();self.assertEqual(st.publish(C,K,I,-1.5,O),p);self.assertEqual(path.read_bytes(),before)
    self.assertEqual(root.stat().st_mode&0o777,0o700);self.assertEqual(path.stat().st_mode&0o777,0o600)
    with self.assertRaises(ValueError):st.publish(C,K,I,-2.0,O)
    self.assertEqual(path.read_bytes(),before)
   with m.CacheStore(root) as st:self.assertEqual(st.read_cache_only(C,K,I),p)
 def test_fault_points(self):
  for point in ['after_temp_write','before_link','after_link']:
   with tempfile.TemporaryDirectory() as d:
    root=pathlib.Path(d)/'cache'
    with m.CacheStore(root) as st:
     def fault(p):
      if p==point:raise RuntimeError('synthetic interruption')
     with self.assertRaises(RuntimeError):st.publish(C,K,I,-1.0,O,fault=fault)
    with m.CacheStore(root) as st:
     if point=='after_link':self.assertEqual(st.read_cache_only(C,K,I)['value'],-1.0)
     else:
      with self.assertRaises(FileNotFoundError):st.read_cache_only(C,K,I)
    self.assertFalse(any(p.name.startswith('.pending-') for p in root.iterdir()))
 def test_missing_never_scores(self):
  with tempfile.TemporaryDirectory() as d,m.CacheStore(pathlib.Path(d)/'cache') as st:
   with self.assertRaises(FileNotFoundError):st.read_cache_only(C,K,I)
 def test_corruption_never_miss(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)/'cache'
   with m.CacheStore(root) as st:
    st.publish(C,K,I,-1.0,O);path=root/(K+'.json');path.write_text('{}')
    with self.assertRaises(ValueError):st.read_cache_only(C,K,I)
    with self.assertRaises(ValueError):st.publish(C,K,I,-1.0,O)
    self.assertEqual(path.read_text(),'{}')
 def test_symlink_and_path_refusal(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)/'cache';target=pathlib.Path(d)/'target';target.mkdir(mode=0o700);root.symlink_to(target,target_is_directory=True)
   with self.assertRaises(ValueError):m.CacheStore(root)
   root.unlink()
   with m.CacheStore(root) as st:
    (root/(K+'.json')).symlink_to(pathlib.Path(d)/'absent')
    with self.assertRaises(OSError):st.read_cache_only(C,K,I)
    with self.assertRaises(ValueError):st.read_cache_only(C,'../escape',I)
 def test_permissions_reject(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)/'cache';root.mkdir(mode=0o755)
   with self.assertRaises(ValueError):m.CacheStore(root)
   root.chmod(0o700)
   with m.CacheStore(root) as st:
    st.publish(C,K,I,-1.0,O);(root/(K+'.json')).chmod(0o644)
    with self.assertRaises(ValueError):st.read_cache_only(C,K,I)
 def test_duplicate_json_reject(self):
  with self.assertRaises(ValueError):m.loads(b'{"value":1,"value":2}')
 def test_FIFO_rejected_without_blocking_subprocess(self):
  import subprocess,sys
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)/'cache';root.mkdir(mode=0o700);os.mkfifo(root/(K+'.json'),mode=0o600)
   code="import importlib.util,pathlib; s=importlib.util.spec_from_file_location('cs',%r); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); st=m.CacheStore(%r); st.read_cache_only(%r,%r,%r)"%(str(p),str(root),C,K,I)
   result=subprocess.run([sys.executable,'-c',code],capture_output=True,timeout=3)
   self.assertNotEqual(result.returncode,0);self.assertIn(b'private bounded regular score',result.stderr)
 def test_window_actual_readback(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)/'cache'
   with m.CacheStore(root) as st:p=st.publish(C,K,I,-1.0,O)
   ref={'contract_sha256':C,'cache_key':K,'input_id':I,'payload_sha256':p['payload_sha256']}
   with m.CacheStore(root) as st:
    self.assertEqual(st.verify_window([ref])['actual_score_files_verified'],1)
    with self.assertRaises(ValueError):st.verify_window([ref,ref])
    (root/(K+'.json')).unlink()
    with self.assertRaises(FileNotFoundError):st.verify_window([ref])
if __name__=='__main__':unittest.main()
