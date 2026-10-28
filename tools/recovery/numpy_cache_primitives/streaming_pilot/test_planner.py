import copy,sys,unittest
from pathlib import Path
import planner as p
import transactions as t
H='a'*64
S='b'*64
def scene(name='a',supported=True):
 rows=[]
 for mode,cond in [('pureinitial','cold'),('pureinitial','given'),('mixed','cold'),('mixed','given')]:
  rows.append({'condition_id':name+mode+cond,'mode':mode,'condition':cond,'source_sha256':H,'prefix':[2],'candidates':[{'index':i,'input_id':p.input_id([2],[3+i])if supported else None,'tail':[3+i]if supported else [1],'supported':supported,'reasons':[]if supported else ['candidate_OOV']}for i in range(3)]})
 return {'scene_id':name,'split':'train','source_sha256':H,'conditions':rows}
def plan():return p.plan([scene()],H,{'prefix':[2],'tail':[99]},unique_per_chunk=2)
def cost(n=1):return {k:n for k in t.FIELDS}
class Tests(unittest.TestCase):
 def test_first_scene_four_fullpools_and_dedup(self):
  x=p.plan([scene('z'),scene('b'),scene('a',False)],H,{'prefix':[2],'tail':[99]},unique_per_chunk=2)
  self.assertEqual(x['selected_scene_id'],'b');self.assertEqual(len(x['conditions']),4);self.assertEqual(len(x['inputs']),3);self.assertEqual([len(r['candidates'])for r in x['conditions']],[3]*4);self.assertEqual([c['score_calls']for c in x['chunks']],[4,3]);self.assertEqual(x['total_initial_score_calls'],7)
 def test_dev_gold_missingpool_and_reordering_rejected(self):
  for mutate in [lambda s:s.update(split='student_dev'),lambda s:s.update(gold=0),lambda s:s['conditions'].pop(),lambda s:s['conditions'][0]['candidates'].reverse()]:
   s=scene();mutate(s)
   with self.assertRaises(ValueError):p.plan([s],H,{'prefix':[2],'tail':[99]})
 def test_context_unk_is_preserved_not_candidate_oov(self):
  s=scene();r=s['conditions'][1];r['prefix']=[2,1]
  for c in r['candidates']:c['input_id']=p.input_id(r['prefix'],c['tail'])
  x=p.plan([s],H,{'prefix':[2],'tail':[99]});self.assertTrue(any(i['prefix']==[2,1]for i in x['inputs']))
  r['candidates'][0]['input_id']='c'*64
  with self.assertRaises(ValueError):p.plan([s],H,{'prefix':[2],'tail':[99]})
 def test_bad_chunk_limits(self):
  for n in (0,33,True):
   with self.assertRaises(ValueError):p.plan([scene()],H,{'prefix':[2],'tail':[99]},unique_per_chunk=n)
 def test_key_exact_existing_numpy_schema(self):
  sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import contract_numpy_v2 as c
  prefix=[2];tail=[3];iid=p.input_id(prefix,tail);self.assertEqual(iid,c.input_identity(prefix,tail));self.assertEqual(p.cache_key(H,iid,prefix,tail),c.digest({'schema':'numpy-teacher-numeric-key-v2','numeric_contract_sha256':H,'input_id':iid,'prefix':prefix,'tail':tail}))
 def ledger(self):return t.genesis_ledger(H,cost(3),S,{'a':{'kind':'initial','cost':cost(),'parent_completed':None},'b':{'kind':'recompute','cost':cost(2),'parent_completed':'a'}})
 def receipt(self,op='a',payload=H):return {'operation_id':op,'plan_sha256':H,'status':'CLOSED','source_identity_sha256':S,'payload_sha256':payload}
 def test_unknown_blocks_and_no_automatic_miss(self):
  a=t.issue(self.ledger(),'a',cost(),kind='initial');self.assertTrue(a['unknown'])
  with self.assertRaises(ValueError):t.issue(a,'b',cost(2),kind='recompute',parent_completed='a')
  with self.assertRaises(ValueError):t.close(a,'a',self.receipt(),'c'*64)
  self.assertEqual(a['used'],cost())
 def test_closed_recompute_cumulative_budget_exact(self):
  a=t.issue(self.ledger(),'a',cost(),kind='initial');a=t.close(a,'a',self.receipt(),p.digest(self.receipt()));b=t.issue(a,'b',cost(2),kind='recompute',parent_completed='a');self.assertEqual(b['used'],cost(3))
  bad=self.receipt('b','c'*64)
  with self.assertRaises(ValueError):t.close(b,'b',bad,p.digest(bad))
  good=self.receipt('b');b=t.close(b,'b',good,p.digest(good));self.assertFalse(b['unknown'])
  with self.assertRaises(ValueError):t.issue(b,'a',cost(),kind='initial')
 def test_wrong_declared_cost_or_source_rejected(self):
  with self.assertRaises(ValueError):t.issue(self.ledger(),'a',cost(0),kind='initial')
  a=t.issue(self.ledger(),'a',cost(),kind='initial');bad=self.receipt();bad['source_identity_sha256']=H
  with self.assertRaises(ValueError):t.close(a,'a',bad,p.digest(bad))
 def test_labels_require_all_exact_values_full_pools(self):
  x=plan();records=[{'input_id':i['input_id'],'cache_key':i['cache_key'],'contract_sha256':H,'value':-1.0,'status':'CLOSED','closure_sha256':S}for i in x['inputs']];pins={r['input_id']:p.digest(r)for r in records};out=t.labels_complete(x,records,pins,expected_plan_sha=x['plan_sha256']);self.assertEqual(len(out['conditions']),4);self.assertEqual([len(r['candidates'])for r in out['conditions']],[3]*4);self.assertFalse(out['student_update_authorized'])
  with self.assertRaises(ValueError):t.labels_complete(x,records[:-1],pins,expected_plan_sha=x['plan_sha256'])
  records[0]['status']='UNKNOWN'
  with self.assertRaises(ValueError):t.labels_complete(x,records,{r['input_id']:p.digest(r)for r in records},expected_plan_sha=x['plan_sha256'])
 def expected(self):
  return {'experiment_sha256':H,'source_identity_sha256':S,'plan_sha256':H,'state_schema':'synthetic-full-state-v1','parent_checkpoint_sha256':None,'checkpoint_sha256':H,'complete_state_sha256':S,'cursor':{'epoch':0,'scene_index':0,'condition_index':0,'completed_updates':0}}
 def backup(self):
  e=self.expected();identity={k:e[k]for k in ('experiment_sha256','source_identity_sha256','plan_sha256')}
  return {'checkpoint':dict(e,status='SEALED',state_kind='GENESIS_SEALED'),'remote':dict(identity,status='DOWNLOADED_EXACT',checkpoint_sha256=H,artifact_sha256=S,raw_teacher_included=False,frozen_dependencies_included=False),'restore':dict(e,status='FRESH_PROCESS_RESTORED'),'head':dict(identity,status='CAS_READBACK_VERIFIED',previous_generation=7,generation=8,previous_head_sha256=H,checkpoint_sha256=H,artifact_sha256=S,new_head_sha256=S,cursor=e['cursor'],parent_checkpoint_sha256=None)}
 def test_genesis_remote_restore_head_all_required(self):
  r=self.backup();pins={k:p.digest(v)for k,v in r.items()};self.assertTrue(t.next_update_gate('GENESIS_SEALED',r,pins,expected_generation=7,expected_head_sha=H,expected_state=self.expected())['next_update_ready'])
  for field in ('remote','restore','head'):
   q=copy.deepcopy(r);q[field]['status']='UNKNOWN'
   with self.assertRaises(ValueError):t.next_update_gate('GENESIS_SEALED',q,{k:p.digest(v)for k,v in q.items()},expected_generation=7,expected_head_sha=H,expected_state=self.expected())
  with self.assertRaises(ValueError):t.next_update_gate('GENESIS_SEALED',r,pins,expected_generation=8,expected_head_sha=H,expected_state=self.expected())
 def recordset(self,x):
  rows=[{'input_id':i['input_id'],'cache_key':i['cache_key'],'contract_sha256':H,'value':-1.0,'status':'CLOSED','closure_sha256':S}for i in x['inputs']]
  return rows,{r['input_id']:p.digest(r)for r in rows}
 def test_external_plan_hash_rejects_deep_mutation(self):
  original=plan();records,pins=self.recordset(original);expected=original['plan_sha256']
  for mutate in (lambda x:x['conditions'][0]['candidates'].pop(),lambda x:x['conditions'][0]['candidates'].reverse(),lambda x:x['conditions'][0]['candidates'].append(copy.deepcopy(x['conditions'][0]['candidates'][0])),lambda x:x['conditions'][0].update(weight_denominator=3),lambda x:x['inputs'][0]['tail'].append(9)):
   x=copy.deepcopy(original);mutate(x)
   with self.assertRaises(ValueError):t.labels_complete(x,records,pins,expected_plan_sha=expected)
   x['plan_sha256']=p.digest({k:v for k,v in x.items()if k!='plan_sha256'})
   with self.assertRaises(ValueError):t.labels_complete(x,records,pins,expected_plan_sha=expected)
 def test_sentinel_extra_fields_rejected(self):
  for name in ('gold','score','input_id','label'):
   sentinel={'prefix':[2],'tail':[99],name:0}
   with self.assertRaises(ValueError):p.plan([scene()],H,sentinel)
 def test_foreign_source_plan_cursor_or_extra_field_cannot_ready(self):
  for target,field,value in [('checkpoint','source_identity_sha256','c'*64),('restore','plan_sha256','c'*64),('head','experiment_sha256','c'*64),('checkpoint','parent_checkpoint_sha256','c'*64),('restore','complete_state_sha256','c'*64),('restore','state_schema','foreign'),('checkpoint','extra',True),('remote','extra',True),('restore','extra',True),('head','extra',True)]:
   r=copy.deepcopy(self.backup());r[target][field]=value
   with self.assertRaises(ValueError):t.next_update_gate('GENESIS_SEALED',r,{k:p.digest(v)for k,v in r.items()},expected_generation=7,expected_head_sha=H,expected_state=self.expected())
  for target in ('checkpoint','restore','head'):
   q=copy.deepcopy(self.backup());q[target]['cursor']=dict(self.expected()['cursor'],epoch=False)
   with self.assertRaises(ValueError):t.next_update_gate('GENESIS_SEALED',q,{k:p.digest(v)for k,v in q.items()},expected_generation=7,expected_head_sha=H,expected_state=self.expected())
   r=copy.deepcopy(self.backup());r[target]['cursor']=dict(self.expected()['cursor'],scene_index=1)
   with self.assertRaises(ValueError):t.next_update_gate('GENESIS_SEALED',r,{k:p.digest(v)for k,v in r.items()},expected_generation=7,expected_head_sha=H,expected_state=self.expected())
if __name__=='__main__':unittest.main()
