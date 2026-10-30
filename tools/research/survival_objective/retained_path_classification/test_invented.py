import unittest,types,copy
from classify import classify
# Two complete target segmentations: (a,b,c) or (ab,c). No fullspan edge.
G={'total':3,'edges':[(0,1,0),(1,2,1),(2,3,2),(0,2,3),(0,1,4)],'entries':[{'text':t,'weight':0.}for t in ['a','b','c','ab','x']]}
N=[[2,1,0,1,0,0.,0.],[3,2,1,2,1,0.,0.],[4,3,2,3,2,0.,0.],[5,1,0,2,3,0.,0.],[6,5,2,3,2,0.,0.],[7,1,0,1,4,0.,0.]]
def snapshot(ids):return {'nodes':copy.deepcopy(N),'pool_ids':ids,'pool':[{'node':i,'text':'abc'}for i in ids]}
def obj(pairs):return types.SimpleNamespace(PENALTY=0.,selected_boundaries=lambda *a:(pairs,{'selected':len(pairs or [])}))
class Tests(unittest.TestCase):
 def test_multiple_segmentations_any_surviving_path(self):
  r=classify(G,'abc',[0.]*5,snapshot([4,6]),obj([((0,),(4,)),((3,),(4,))]));self.assertEqual(r['surviving_target_paths'],2);self.assertEqual(r['on_surviving_path_positive_pairs'],2)
 def test_alternative_text_prefix_not_edge_prefix(self):
  r=classify(G,'abc',[0.]*5,snapshot([6]),obj([((0,),(4,))]));self.assertEqual(r['off_surviving_path_positive_pairs'],1)
 def test_shared_prefix_classified_on_path(self):
  r=classify(G,'abc',[0.]*5,snapshot([4]),obj([((0,1),(4,))]));self.assertEqual(r['max_hinge_category'],'on_surviving_path')
 def test_nonpositive_and_reporting_threshold(self):
  r=classify(G,'abc',[2.,0.,0.,0.,0.],snapshot([4]),obj([((0,),(4,))]));self.assertEqual(r['positive_pairs'],0);self.assertEqual(r['max_hinge_category'],'none_above_threshold')
 def test_missing_target_not_called_off_path(self):
  r=classify(G,'abc',[0.]*5,snapshot([]),obj([((0,),(4,))]));self.assertEqual(r['no_surviving_target_positive_pairs'],1);self.assertEqual(r['off_surviving_path_positive_pairs'],0)
 def test_broken_node_parent_fail_closed(self):
  s=snapshot([4]);s['nodes'][1][1]=99
  with self.assertRaises(ValueError):classify(G,'abc',[0.]*5,s,obj([]))
 def test_mixed_max_hinge_tie(self):
  r=classify(G,'abc',[0.]*5,snapshot([4]),obj([((0,),(4,)),((3,),(4,))]));self.assertEqual(r['max_hinge_category'],'mixed')
 def test_unreachable_no_selected(self):
  r=classify(G,'abc',[0.]*5,snapshot([]),obj(None));self.assertFalse(r['dp_reachable']);self.assertEqual(r['selected_pairs'],0)
if __name__=='__main__':unittest.main()
