import ast
import inspect
from pathlib import Path
import sys
import unittest
import gru_numpy as g
import gru_synthetic_checks as c

class Tests(unittest.TestCase):
 def test_no_numerical_import(self):
  for name in ('numpy','torch','_ctypes'):self.assertNotIn(name,sys.modules)
 def test_source_import_allowlist(self):
  for module in (g,c):
   tree=ast.parse(inspect.getsource(module))
   for node in ast.walk(tree):
    if isinstance(node,ast.Import):self.assertTrue(all(n.name in ('math','gru_numpy') for n in node.names))
    if isinstance(node,ast.ImportFrom):self.fail('no import-from needed')
 def test_geometry(self):
  self.assertEqual(g.parameter_count(3,2),42);self.assertEqual(g.parameter_count(128,128),99072)
  for d,h in ((True,2),(2,False),(0,2),(-1,2)):
   with self.assertRaises(ValueError):g.parameter_count(d,h)
 def test_gradient_budget(self):
  self.assertEqual(47+53+104,204)
  self.assertEqual(sum(2*n+1 for n in (47,53,104))+3,414)
  self.assertEqual((2*47+2)*1+(2*53+2)*3+(2*104+2)*6,1680)
 def test_scalar_oracle_nonzero_recurrent_bias_reset(self):
  p={'weight_ih':[[0.],[0.],[0.]],'weight_hh':[[0.],[0.],[0.]],'bias_ih':[0.,0.,0.],'bias_hh':[0.,0.,2.]}
  # r=z=.5; tanh(.5*2), not tanh(2): catches reset-before-affine confusion.
  import math
  self.assertAlmostEqual(c.scalar_oracle(p,[0.],[0.])[0],0.5*math.tanh(1.))
 def test_fixed_gradient_tolerances(self):
  self.assertEqual((c.EPSILON,c.ATOL,c.RTOL),(1e-5,1e-7,1e-5))
if __name__=='__main__':unittest.main()
