import math,sys,unittest
import independent_gru_oracle as o
class Tests(unittest.TestCase):
 def test_no_numeric_backend(self):
  self.assertNotIn('numpy',sys.modules);self.assertNotIn('torch',sys.modules)
 def test_zero_weights(self):
  p=o.params(0)
  for path in o.leaves(p):o.put(p,path,0.)
  self.assertEqual(o.cell([.2,.3,.4],[-.2,.6],p),[-.1,.3])
 def test_reset_bias_variant(self):
  p=o.params(0)
  for path in o.leaves(p):o.put(p,path,0.)
  p['b_recurrent'][4]=.8;a=o.cell([0.,0.,0.],[0.,0.],p)
  self.assertAlmostEqual(a[0],.5*math.tanh(.4),places=15);self.assertNotAlmostEqual(a[0],.5*math.tanh(.8),places=6)
 def test_gradient_count_and_stability(self):
  f=o.fixture();self.assertEqual([c['gradient_count'] for c in f['cases']],[53,101]);self.assertTrue(all(c['half_step_max_abs_difference']<1e-7 for c in f['cases']))
 def test_chain_context_gradient_changes(self):
  a={tuple(r['path']):r['value'] for r in o.gradients(o.payload(False),o.EPS)};b={tuple(r['path']):r['value'] for r in o.gradients(o.payload(True),o.EPS)}
  self.assertGreater(abs(a['context_h0',0]-b['context_h0',0]),1e-5)
if __name__=='__main__':unittest.main()
