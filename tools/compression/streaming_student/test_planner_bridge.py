"""Cross-package integration uses ONLY synthetic lists and fake receipts."""
import copy,sys,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
PLANNER=HERE.parents[1]/'recovery'/'numpy_cache_primitives'/'streaming_pilot'
sys.path.insert(0,str(PLANNER))
import planner as p
import transactions as tx
import planner_bridge as bridge
from state_codec import digest
from label_contract import validate_labels
from test_contracts import FakeBackend,H
S='b'*64

def data():
    rows=[]
    for i,(mode,condition)in enumerate((('pureinitial','cold'),('pureinitial','given'),('mixed','cold'),('mixed','given'))):
        rows.append({'condition_id':'fake-'+str(i),'mode':mode,'condition':condition,'source_sha256':H,'prefix':[2],'candidates':[{'index':j,'tail':[3+j],'supported':True,'reasons':[],'input_id':p.input_id([2],[3+j])}for j in range(2)]})
    plan=p.plan([{'scene_id':'synthetic-test-only','split':'train','source_sha256':H,'conditions':rows}],H,{'prefix':[2],'tail':[99]})
    scores=[{'input_id':r['input_id'],'cache_key':r['cache_key'],'contract_sha256':H,'value':-1.,'status':'CLOSED','closure_sha256':S}for r in plan['inputs']]
    records=[{'id':r['condition_id'],'context':[[0.]],'candidates':[[[1.]],[[2.]]],'teacher_scores':[-1.,-1.],'gold_index':None}for r in rows]
    provenance={k:H for k in ('teacher_identity_sha256','split_manifest_sha256','final_exclusion_policy_sha256','frozen_embeddings_sha256','embedding_binding_sha256','source_identity_sha256')};provenance['teacher_domain']='fake-data-testing-real-schema-only'
    return plan,scores,records,provenance

def candidate():
    plan,scores,records,prov=data();c=bridge.materialize_candidate(plan,plan['plan_sha256'],tx,scores,{r['input_id']:digest(r)for r in scores},records,{r['id']:digest(r)for r in records},FakeBackend(),prov);return c,records
class BridgeTests(unittest.TestCase):
    def test_full_pool_aliases_and_pending_materialization(self):
        c,r=candidate();self.assertFalse(c['student_update_authorized']);self.assertEqual([len(x['candidate_keys'])for x in c['scope']['records']],[2]*4)
        with self.assertRaises(ValueError):validate_labels(c['scope'],digest(c['scope']),c['labels'],digest(c['labels']),r,FakeBackend())
        closed=copy.deepcopy(c['labels']);closed['status']='INDEPENDENTLY_CLOSED_COMPLETE'
        review={'status':'MATERIALIZATION_INDEPENDENTLY_VERIFIED','candidate_sha256':digest(c),'scope_sha256':digest(c['scope']),'closed_labels_sha256':digest(closed),'materialized_record_manifest_sha256':c['materialized_record_manifest_sha256'],'source_identity_sha256':c['source_identity_sha256']}
        approved=bridge.admit_materialization(c,digest(c),review,digest(review));self.assertEqual(approved,closed);self.assertTrue(validate_labels(c['scope'],digest(c['scope']),approved,digest(approved),r,FakeBackend()))
    def test_plan_mutation_cannot_self_reseal(self):
        plan,scores,r,prov=data();expected=plan['plan_sha256'];plan['conditions'][0]['candidates'].pop();plan['plan_sha256']=p.digest({k:v for k,v in plan.items()if k!='plan_sha256'})
        with self.assertRaises(ValueError):bridge.materialize_candidate(plan,expected,tx,scores,{x['input_id']:digest(x)for x in scores},r,{x['id']:digest(x)for x in r},FakeBackend(),prov)
    def test_changed_payload_cannot_inherit_old_seal(self):
        plan,scores,r,prov=data();pins={x['id']:digest(x)for x in r};r[0]['teacher_scores'][0]=-2.
        with self.assertRaises(ValueError):bridge.materialize_candidate(plan,plan['plan_sha256'],tx,scores,{x['input_id']:digest(x)for x in scores},r,pins,FakeBackend(),prov)
    def proof_fixture(self):
        import types
        state={'cursor':{'completed_updates':0,'next_batch':0},'fake':'allstate'};loop=types.SimpleNamespace(tip=H,state=state,identity={'schema_sha256':S})
        expected={'experiment_sha256':H,'source_identity_sha256':S,'plan_sha256':H,'state_schema':S,'parent_checkpoint_sha256':None,'checkpoint_sha256':H,'complete_state_sha256':digest(state),'cursor':{'epoch':0,'scene_index':0,'condition_index':0,'completed_updates':0}}
        identity={k:expected[k]for k in ('experiment_sha256','source_identity_sha256','plan_sha256')}
        receipts={'checkpoint':dict(expected,status='SEALED',state_kind='GENESIS_SEALED'),'remote':dict(identity,status='DOWNLOADED_EXACT',checkpoint_sha256=H,artifact_sha256=S,raw_teacher_included=False,frozen_dependencies_included=False),'restore':dict(expected,status='FRESH_PROCESS_RESTORED'),'head':dict(identity,status='CAS_READBACK_VERIFIED',previous_generation=7,generation=8,previous_head_sha256=H,checkpoint_sha256=H,artifact_sha256=S,new_head_sha256=S,cursor=expected['cursor'],parent_checkpoint_sha256=None)}
        return loop,expected,receipts
    def test_backup_bridge_binds_actual_state_and_admitted_anchors(self):
        loop,e,r=self.proof_fixture();pins={k:digest(v)for k,v in r.items()};out=bridge.backup_proof_from_planner(tx,'GENESIS_SEALED',r,pins,loop,H,S,7,H,e,H,None)
        self.assertEqual(out['state_sha256'],digest(loop.state));self.assertEqual(out['head_after_generation'],8)
        for key in ('experiment_sha256','source_identity_sha256','plan_sha256'):
            bad=copy.deepcopy(e);bad[key]='c'*64
            with self.subTest(k=key),self.assertRaises(ValueError):bridge.backup_proof_from_planner(tx,'GENESIS_SEALED',r,pins,loop,H,S,7,H,bad,H,None)
    def test_cursor_partial_or_cross_scene_rejected(self):
        loop,e,r=self.proof_fixture();e['cursor']['condition_index']=1
        with self.assertRaises(ValueError):bridge.backup_proof_from_planner(tx,'GENESIS_SEALED',r,{k:digest(v)for k,v in r.items()},loop,H,S,7,H,e,H,None)
if __name__=='__main__':unittest.main()
