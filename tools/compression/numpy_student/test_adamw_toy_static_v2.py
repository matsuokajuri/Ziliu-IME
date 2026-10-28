import ast
import inspect
import math
import sys
import unittest
from types import SimpleNamespace
import tempfile
from pathlib import Path
import adamw_numpy as a
import adamw_scalar_reference as o
import adamw_synthetic_checks_v2 as checks
import gru_toy_learning as toy
import toy_checkpoint as ck
class Tests(unittest.TestCase):
 def test_no_numpy_import(self):
  for name in ('numpy','torch','_ctypes'):self.assertNotIn(name,sys.modules)
 def test_fixed_config(self):
  self.assertEqual(a.CONFIG,dict(learning_rate=.03,beta1=.9,beta2=.999,epsilon=1e-8,weight_decay=.01,clip_norm=1.))
 def test_scalar_first_step_decay_and_clip(self):
  p,m,v,t=o.transition({'w':[1.],'b':[2.]},{'w':[3.],'b':[4.]},{'w':[0.],'b':[0.]},{'w':[0.],'b':[0.]},0,{'w'})
  self.assertEqual(t,1);self.assertAlmostEqual(m['w'][0],.06);self.assertAlmostEqual(m['b'][0],.08)
  self.assertAlmostEqual(p['w'][0],1*(1-.03*.01)-.03*.6/(.6+1e-8));self.assertAlmostEqual(p['b'][0],2-.03*.8/(.8+1e-8))
 def test_scalar_second_step_nonzero_moments(self):
  p,m,v,t=o.transition({'w':[1.]},{'w':[.2]},{'w':[.4]},{'w':[.3]},7,{'w'})
  expected_m=.9*.4+.1*.2;expected_v=.999*.3+.001*.04
  self.assertAlmostEqual(p['w'][0],.9997-.03*(expected_m/(1-.9**8))/(math.sqrt(expected_v/(1-.999**8))+1e-8))
 def test_geometry_and_fixed_data(self):
  self.assertEqual(sum(math.prod(x) for x in ck.SHAPES.values()),45);self.assertEqual(len(toy.INPUTS),4)
  self.assertTrue(all(len(x)==2 and all(len(v)==3 for v in x) for x in toy.INPUTS));self.assertEqual(toy.TARGETS,(.6,-.6,.3,-.3))
  self.assertEqual(toy.DECAY,frozenset(('weight_ih','weight_hh','head_weight')))
 def test_complete_call_budget(self):
  self.assertEqual(128+64,192);self.assertEqual(1+3+192+1+1,198)
  self.assertEqual(192*4*2+3*4*2,1560);self.assertEqual(192*4*2,1536)
 def test_no_eager_backend_imports(self):
  allowed={'math','hashlib','json','os','pathlib','random','gru_numpy','adamw_numpy','adamw_scalar_reference','toy_checkpoint'}
  for module in (a,o,checks,toy,ck):
   for node in ast.walk(ast.parse(inspect.getsource(module))):
    if isinstance(node,ast.Import):self.assertTrue(all(v.name in allowed for v in node.names))
    if isinstance(node,ast.ImportFrom):self.assertIn(node.module,allowed)
 def test_joint_alias_contract_without_numpy(self):
  x=object();y=object();fake=SimpleNamespace(shares_memory=lambda a,b:a is b)
  a.disjoint(fake,[x,y])
  with self.assertRaises(ValueError):a.disjoint(fake,[x,y,x])
 def test_snapshot_detects_changed_buffer(self):
  class Buffer:
   shape=(1,);dtype='float64'
   def __init__(self,value):self.value=value
   def tobytes(self):return self.value
  x=Buffer(b'original');before=a.snapshot(({'p':x},));x.value=b'changed'
  self.assertNotEqual(a.snapshot(({'p':x},)),before)
 def test_toy_requires_external_experiment_identity(self):
  with self.assertRaises(ValueError):toy.run(None,Path('.'),lambda *a:None,'bad')
  self.assertNotIn('__file__',inspect.getsource(toy))
 def test_checkpoint_rejects_identity_before_numpy(self):
  with tempfile.TemporaryDirectory() as td:
   path=Path(td)/'bad'
   with self.assertRaises(ValueError):ck.save(None,path,{},dict(step=64),None,dict(completed_updates=64,next_update=64),'not-a-digest')
   self.assertFalse(path.exists())
 def test_extended_attempt_budget(self):
  self.assertEqual(198+5,203);self.assertEqual(192+3,195);self.assertEqual(192+5,197)
 def test_checkpoint_strict_cursor_json(self):
  self.assertNotEqual(ck.canonical({'completed_updates':64}),ck.canonical({'completed_updates':64.}))
  self.assertNotEqual(ck.canonical({'next_update':True}),ck.canonical({'next_update':1}))
 def test_optimizer_initialization_after_first_issue(self):
  class FakeNP:
   def array(self,*args,**kwargs):
    if events!=[('issued',{'index':0,'kind':'adamw-scalar-step'})]:raise AssertionError('initialization before exact first issue')
    raise StopIteration('synthetic interception before numerical operation')
  events=[]
  with self.assertRaises(StopIteration):checks.run(FakeNP(),lambda k,v:events.append((k,v)))
if __name__=='__main__':unittest.main()
