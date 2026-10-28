import unittest
import math
import objective_reference as r
class Tests(unittest.TestCase):
 def test_geometry(self):
  g=r.geometry();self.assertEqual(sum(v for k,v in g.items() if k!='frozen_embedding'),288769);self.assertEqual(sum(g.values()),1861633)
 def test_score_gradient(self):
  s=[-.2,-1.5,-.4];t=[-.6,-1.7,-.9]
  for gold in (None,0,2):
   _,g=r.objective_and_score_gradient(s,t,gold)
   for i in range(3):
    a=s.copy();b=s.copy();a[i]+=1e-5;b[i]-=1e-5
    numeric=(r.objective_and_score_gradient(a,t,gold)[0]-r.objective_and_score_gradient(b,t,gold)[0])/2e-5
    self.assertAlmostEqual(g[i],numeric,places=8)
 def test_shift_and_order(self):
  s=[-.2,-1.5,-.4];t=[-.6,-1.7,-.9]
  loss,g=r.objective_and_score_gradient(s,t,1)
  shifted,sg=r.objective_and_score_gradient([v+17 for v in s],[v-4 for v in t],1)
  self.assertAlmostEqual(loss,shifted);self.assertAlmostEqual(sum(g),0)
  perm=[2,0,1];pl,pg=r.objective_and_score_gradient([s[i] for i in perm],[t[i] for i in perm],2)
  self.assertAlmostEqual(pl,loss)
  for x,y in zip(pg,[g[i] for i in perm]):self.assertAlmostEqual(x,y)
 def test_single_and_missing_gold(self):
  self.assertEqual(r.objective_and_score_gradient([1.],[2.],None),(0.,[0.]))
 def test_large_common_offset_equal_logits(self):
  for offset in (1e16,-1e16,1e300):
   for x in r.logsoftmax([offset]*3):self.assertAlmostEqual(x,-math.log(3))
   loss,g=r.objective_and_score_gradient([offset]*3,[0.]*3,0)
   self.assertAlmostEqual(loss,0.5*math.log(3));self.assertAlmostEqual(sum(g),0.,places=14)
 def test_overflowing_range_rejected(self):
  with self.assertRaises(ValueError):r.logsoftmax([1e308,-1e308])
 def test_bad_values(self):
  for s,t,g in [([],[],None),([float('nan')],[0.],None),([1.],[1.],True),([1.]*133,[1.]*133,None)]:
   with self.assertRaises(ValueError):r.objective_and_score_gradient(s,t,g)
if __name__=='__main__':unittest.main()
