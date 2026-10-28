import hashlib,json,os,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import supervised_command as s
class Tests(unittest.TestCase):
 def manifest(self,code,**changes):
  exe=str(Path(sys.executable).resolve());m={'argv':[exe,'-c',code],'env':{'PYTHONHASHSEED':'0'},'cwd':str(Path.cwd()),'source_sha256':{exe:hashlib.sha256(Path(exe).read_bytes()).hexdigest(),str(Path(s.__file__).resolve()):hashlib.sha256(Path(s.__file__).read_bytes()).hexdigest()},'deadline_seconds':2,'kill_grace_seconds':.1,'stream_cap_bytes':1024};m.update(changes);return m
 def launch(self,root,code,**changes):
  m=self.manifest(code,**changes);return s.run(m,{'approved':True,'manifest_sha256':s.digest(m)},root)
 def test_success_logs_pid_and_exit(self):
  with tempfile.TemporaryDirectory() as td:
   root=Path(td)/'attempt';r=self.launch(root,"import sys;print('hello');print('err',file=sys.stderr)")
   self.assertEqual(r['status'],'EXIT_ZERO');self.assertEqual((root/'stdout.bin').read_bytes(),b'hello\n');self.assertEqual((root/'stderr.bin').read_bytes(),b'err\n');self.assertTrue((root/'CHILD.json').is_file());self.assertTrue(r['cleanup_confirmed_reaped']);self.assertTrue(r['capture_eof_complete'])
 def test_nonzero_not_success(self):
  with tempfile.TemporaryDirectory() as td:self.assertEqual(self.launch(Path(td)/'a','raise SystemExit(7)')['returncode'],7)
 def test_overflow_fails_explicitly(self):
  with tempfile.TemporaryDirectory() as td:
   r=self.launch(Path(td)/'a',"import sys;sys.stdout.write('x'*100000)",stream_cap_bytes=64);self.assertEqual(r['status'],'OUTPUT_OVERFLOW');self.assertTrue(r['streams']['stdout']['truncated']);self.assertEqual(r['streams']['stdout']['retained_bytes'],64)
 def test_deadline_term_ignored_owned_kill(self):
  with tempfile.TemporaryDirectory() as td:
   r=self.launch(Path(td)/'a',"import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(30)",deadline_seconds=.15);self.assertEqual(r['status'],'DEADLINE_EXCEEDED');self.assertEqual(r['returncode'],-9);self.assertLess(r['elapsed_seconds'],2)
 def test_unknown_blocks_retry(self):
  with tempfile.TemporaryDirectory() as td:
   root=Path(td)/'a';root.mkdir();s.receipt(root,'INTENT.json',{});self.assertEqual(s.recover(root),'UNKNOWN_BLOCKED')
   with self.assertRaises(RuntimeError):self.launch(root,"print('not executed')")
 def test_review_and_hash_reject_before_attempt(self):
  with tempfile.TemporaryDirectory() as td:
   m=self.manifest('pass');root=Path(td)/'a'
   with self.assertRaises(RuntimeError):s.run(m,{'approved':True,'manifest_sha256':'0'*64},root)
   self.assertFalse(root.exists());m['source_sha256'][m['argv'][0]]='0'*64
   with self.assertRaises(RuntimeError):s.run(m,{'approved':True,'manifest_sha256':s.digest(m)},root)
   self.assertFalse(root.exists())
 def test_intent_precedes_spawn_and_pid_receipt_failure_cleans_owned(self):
  with tempfile.TemporaryDirectory() as td:
   root=Path(td)/'a';real=s.receipt
   def receipt(path,name,value):
    if name=='CHILD.json':
     self.assertTrue((root/'INTENT.json').is_file());raise OSError('synthetic PID receipt failure')
    return real(path,name,value)
   with patch.object(s,'receipt',receipt),self.assertRaisesRegex(OSError,'synthetic PID'):
    self.launch(root,'import time;time.sleep(30)')
   r=json.loads((root/'TERMINAL.json').read_text());self.assertTrue(r['cleanup_confirmed_reaped']);self.assertEqual(r['status'],'SUPERVISOR_FAILURE');self.assertFalse(r['capture_eof_complete']);self.assertTrue(all(x['observed_bytes_are_lower_bound'] for x in r['streams'].values()))
 def test_timeout_does_not_signal_unrelated_process(self):
  import subprocess
  other=subprocess.Popen([str(Path(sys.executable).resolve()),'-c','import time;time.sleep(5)'],start_new_session=True)
  try:
   with tempfile.TemporaryDirectory() as td:self.launch(Path(td)/'a','import time;time.sleep(30)',deadline_seconds=.1)
   self.assertIsNone(other.poll())
  finally:other.terminate();other.wait(timeout=2)
 def test_exited_leader_eof_does_not_hide_live_owned_descendant(self):
  code="import os,time;pid=os.fork();\nif pid==0:\n os.write(1,(str(os.getpid())+'\\n').encode());os.close(1);os.close(2);time.sleep(.5);os._exit(0)\nelse:\n time.sleep(.03);os._exit(0)"
  with tempfile.TemporaryDirectory() as td:
   root=Path(td)/'a';r=self.launch(root,code);pid=int((root/'stdout.bin').read_text().strip())
   self.assertNotEqual(r['status'],'EXIT_ZERO');self.assertTrue(any(o['members'] for o in r['owned_group_observations']))
   path=Path(f'/proc/{pid}/stat')
   if path.exists():self.assertEqual(path.read_text().split(')')[-1].split()[0],'Z')
   if not r['owned_group_clear_before_reap']:self.assertTrue(r['status'].startswith('UNKNOWN'))
 def test_capture_read_failure_is_lower_bound_not_eof(self):
  real=s.os.read
  def read(fd,n):
   data=real(fd,n)
   if b'capture-failure-marker' in data:raise OSError('synthetic capture read failure')
   return data
  with tempfile.TemporaryDirectory() as td:
   root=Path(td)/'a'
   with patch.object(s.os,'read',read),self.assertRaisesRegex(OSError,'synthetic capture'):
    self.launch(root,"import time;print('capture-failure-marker',flush=True);time.sleep(.5)")
   r=json.loads((root/'TERMINAL.json').read_text());self.assertFalse(r['capture_eof_complete']);self.assertTrue(r['streams']['stdout']['observed_bytes_are_lower_bound']);self.assertTrue(r['cleanup_confirmed_reaped']);self.assertTrue(r['owned_group_clear_before_reap'])
if __name__=='__main__':unittest.main()
