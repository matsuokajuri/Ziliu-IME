import ast,copy,inspect,math,sys,unittest
from pathlib import Path
import model,head_oracle

class StaticModelTests(unittest.TestCase):
 def test_geometry(self):
  self.assertEqual(model.parameter_count(),288769);self.assertEqual(model.shapes()['head_weight'],(514,128));self.assertEqual(len(model.shapes()),13)
 def test_geometry_small(self):
  self.assertEqual(model.parameter_count(3,2),6+2*(12+12+12)+20+2+2+1)
 def test_bad_geometry(self):
  for d,h in [(True,2),(2,0),(2,1.0),(-1,2)]:
   with self.assertRaises(ValueError):model.shapes(d,h)
 def test_imports_no_numeric(self):
  tree=ast.parse(Path(model.__file__).read_text());self.assertFalse(any(isinstance(x,(ast.Import,ast.ImportFrom)) for x in ast.walk(tree)))
  self.assertNotIn('numpy',sys.modules);self.assertNotIn('torch',sys.modules)
 def test_forward_features(self):
  self.assertEqual(list(inspect.signature(model.forward).parameters),['np','gru','parameters','context_embeddings','candidate_embeddings'])
  s=inspect.getsource(model.forward);self.assertNotIn('softmax',s);self.assertIn('del candidate_cache',s)
 def test_no_io_or_execution(self):
  tree=ast.parse(Path(model.__file__).read_text())
  for node in ast.walk(tree):
   if isinstance(node,ast.Call) and isinstance(node.func,ast.Name):self.assertNotIn(node.func.id,{'open','eval','exec','compile','__import__'})
 def test_loss_single_complete_pool(self):
  tree=ast.parse(inspect.getsource(model.pool_loss_and_gradients))
  calls=[n for n in ast.walk(tree) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='objective_and_score_gradient']
  self.assertEqual(len(calls),1);self.assertFalse(any(isinstance(n,(ast.For,ast.While)) for n in ast.walk(tree)))
 def test_equal_scene_four_conditions(self):
  self.assertEqual(model.scene_condition_weight(8,4),1/32)
  for s,c in [(0,4),(8,3),(True,4)]:
   with self.assertRaises(ValueError):model.scene_condition_weight(s,c)
 def test_backward_accumulates_context(self):
  s=inspect.getsource(model.backward)
  self.assertIn('dc+=dh0',s);self.assertIn("gradients['projection']+=tail.T@dx",s);self.assertIn("gradients['projection']+=context.T@dx",s)
  self.assertIn('grad_states=np.tile(dm/len(tail)',s)
 def test_head_oracle_finite_differences(self):
  x={'c':[.2,-.3],'states':[[.1,.4],[-.2,.3],[.5,-.1]],'w':[[(i-j-4)*.03 for j in range(2)] for i in range(10)],'b':[.05,-.07],'v':[.3,-.4],'o':.02,'context_length':4}
  gradients=head_oracle.backward(**x)
  def coordinates(v,p=()):
   if isinstance(v,list):
    for i,t in enumerate(v):yield from coordinates(t,p+(i,))
   else:yield p
  def get(v,p):
   for i in p:v=v[i]
   return v
  def alter(d,key,p,delta):
   if not p:d[key]+=delta;return
   v=d[key]
   for i in p[:-1]:v=v[i]
   v[p[-1]]+=delta
  for key,g in gradients.items():
   for p in coordinates(x[key]):
    plus=copy.deepcopy(x);minus=copy.deepcopy(x);alter(plus,key,p,1e-5);alter(minus,key,p,-1e-5)
    actual=(head_oracle.forward(**plus)[0]-head_oracle.forward(**minus)[0])/2e-5
    self.assertAlmostEqual(actual,get(g,p),delta=1e-8)
 def test_head_mean_length_one(self):
  c=[.2];states=[[.3]];w=[[.1] for _ in range(6)];args=(c,states,w,[.0],[.7],.0,1)
  g=head_oracle.backward(*args);_,(f,a)=head_oracle.forward(*args)
  self.assertEqual(f[1],f[2]);self.assertEqual(len(g['states']),1)

if __name__=='__main__':unittest.main()
