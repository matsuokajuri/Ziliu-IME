"""Pure immutable accounting/gates, not durable production transaction or uploader."""
import copy,math,struct
from planner import need,digest,h64
FIELDS={'score_calls','load_calls','getter_calls','guard_checkpoints','retained_bytes','retained_inodes'}
def budget_check(b):need(type(b)is dict and set(b)==FIELDS and all(type(v)is int and v>=0 for v in b.values()),'complete nonnegative cumulative budget')
def genesis_ledger(plan_sha,maximum,source_identity_sha,operation_specs):
 need(h64(plan_sha)and h64(source_identity_sha),'plan/source identity');budget_check(maximum)
 need(type(operation_specs)is dict and operation_specs,'predeclared finite operations')
 for spec in operation_specs.values():
  need(set(spec)=={'kind','cost','parent_completed'},'exact operation declaration');budget_check(spec['cost'])
 need(all(sum(s['cost'][k]for s in operation_specs.values())<=maximum[k]for k in FIELDS),'all declared operations fit cumulative cap')
 return {'schema':'streaming-pilot-ledger-v1','plan_sha256':plan_sha,'source_identity_sha256':source_identity_sha,'operation_specs':copy.deepcopy(operation_specs),'maximum':dict(maximum),'used':{k:0 for k in FIELDS},'operations':{},'unknown':False}
def issue(ledger,op_id,cost,*,kind,parent_completed=None):
 need(not ledger['unknown'],'UNKNOWN freezes dependent pipeline');budget_check(cost)
 need(type(op_id)is str and op_id and op_id not in ledger['operations'],'new exact operation no replay')
 need(op_id in ledger['operation_specs']and ledger['operation_specs'][op_id]=={'kind':kind,'cost':cost,'parent_completed':parent_completed},'exact predeclared operation/cost/lineage')
 need(kind in ('initial','recompute','student','backup','head'),'explicit operation kind')
 if kind=='recompute':need(parent_completed in ledger['operations']and ledger['operations'][parent_completed]['state']=='CLOSED','only proven completed operation can be explicitly recomputed')
 else:need(parent_completed is None,'no unrelated recompute parent')
 need(all(ledger['used'][k]+cost[k]<=ledger['maximum'][k]for k in FIELDS),'finite whole-experiment cumulative budget including recompute and retained bytes')
 new=copy.deepcopy(ledger)
 for k in FIELDS:new['used'][k]+=cost[k]
 new['operations'][op_id]={'kind':kind,'state':'ISSUED','cost':dict(cost),'parent_completed':parent_completed};new['unknown']=True
 return new
def close(ledger,op_id,receipt,expected_receipt_sha):
 need(op_id in ledger['operations']and ledger['operations'][op_id]['state']=='ISSUED','issued exact operation')
 need(h64(expected_receipt_sha)and digest(receipt)==expected_receipt_sha,'external sealed independent receipt hash')
 need(set(receipt)=={'operation_id','plan_sha256','status','source_identity_sha256','payload_sha256'}and receipt['operation_id']==op_id and receipt['plan_sha256']==ledger['plan_sha256']and receipt['status']=='CLOSED'and receipt['source_identity_sha256']==ledger['source_identity_sha256']and h64(receipt['payload_sha256']),'closed receipt binding')
 if ledger['operations'][op_id]['kind']=='recompute':need(receipt['payload_sha256']==ledger['operations'][ledger['operations'][op_id]['parent_completed']]['payload_sha256'],'completed recomputation exact old payload identity')
 new=copy.deepcopy(ledger);new['operations'][op_id].update(state='CLOSED',receipt_sha256=expected_receipt_sha,payload_sha256=receipt['payload_sha256']);new['unknown']=any(r['state']=='ISSUED'for r in new['operations'].values());return new

def labels_complete(plan,records,expected_record_sha,*,expected_plan_sha):
 need(type(plan)is dict and h64(expected_plan_sha)and plan.get('plan_sha256')==expected_plan_sha and digest({k:v for k,v in plan.items()if k!='plan_sha256'})==expected_plan_sha,'externally sealed exact full plan integrity before label assembly')
 need(type(records)is list and len(records)==len(plan['inputs']),'all unique teacher inputs required')
 byid={r['input_id']:r for r in records};need(len(byid)==len(records)and set(byid)==set(expected_record_sha)=={r['input_id']for r in plan['inputs']},'exact scene input inventory')
 for source in plan['inputs']:
  r=byid[source['input_id']]
  need(set(r)=={'input_id','cache_key','contract_sha256','value','status','closure_sha256'},'exact closed scored record')
  need(r['cache_key']==source['cache_key']and r['contract_sha256']==plan['teacher_contract_sha256']and r['status']=='CLOSED'and h64(r['closure_sha256']),'exact contract/cache/independent closure')
  need(h64(expected_record_sha[r['input_id']])and digest(r)==expected_record_sha[r['input_id']],'external expected evidence bytes')
  val=r['value'];need(type(val)is float and math.isfinite(val)and val<=0 and struct.unpack('<f',struct.pack('<f',val))[0]==val,'exact finite FP32 teacher scalar')
 rows=[]
 for condition in plan['conditions']:
  need(condition['weight_numerator']==1 and condition['weight_denominator']==4,'four equal condition weights')
  rows.append({'condition_id':condition['condition_id'],'weight_numerator':1,'weight_denominator':4,'candidates':[{'index':c['index'],'input_id':c['input_id'],'teacher_score':byid[c['input_id']]['value']}for c in condition['candidates']]})
 need(len(rows)==4,'all four pools');return {'schema':'complete-scene-teacher-labels-v1','plan_sha256':plan['plan_sha256'],'scene_id':plan['selected_scene_id'],'conditions':rows,'label_evidence_manifest_sha256':digest(expected_record_sha),'student_update_authorized':False}

def next_update_gate(state,receipts,expected_shas,*,expected_generation,expected_head_sha,expected_state):
 """Independent verifier supplies sealed current experiment/state admission.
 Receipt hashes do not themselves prove remote I/O or independent authority.
 """
 need(state in ('GENESIS_SEALED','UPDATED_LOCAL_SEALED'),'actual complete local state required')
 ef={'experiment_sha256','source_identity_sha256','plan_sha256','state_schema','parent_checkpoint_sha256','checkpoint_sha256','complete_state_sha256','cursor'}
 need(type(expected_state)is dict and set(expected_state)==ef,'exact admitted state lineage schema')
 for k in ('experiment_sha256','source_identity_sha256','plan_sha256','checkpoint_sha256','complete_state_sha256'):need(h64(expected_state[k]),'nonplaceholder admitted state identity')
 need(type(expected_state['state_schema'])is str and expected_state['state_schema'],'state schema identity')
 cursor=expected_state['cursor'];need(type(cursor)is dict and set(cursor)=={'epoch','scene_index','condition_index','completed_updates'}and all(type(v)is int and v>=0 for v in cursor.values()),'exact current cursor contract')
 need(cursor['condition_index']==0,'complete scene boundary only')
 parent=expected_state['parent_checkpoint_sha256']
 if state=='GENESIS_SEALED':need(parent is None and cursor['completed_updates']==0,'genesis parent/cursor')
 else:need(h64(parent)and cursor['completed_updates']>0,'updated parent/cursor')
 need(set(receipts)==set(expected_shas)=={'checkpoint','remote','restore','head'},'complete checkpoint/remote/restore/head evidence')
 for k in receipts:need(h64(expected_shas[k])and digest(receipts[k])==expected_shas[k],'external independently sealed receipt')
 c,r,f,h=(receipts[k]for k in ('checkpoint','remote','restore','head'))
 identity={k:expected_state[k]for k in ('experiment_sha256','source_identity_sha256','plan_sha256')}
 for receipt in (c,r,f,h):need(all(receipt.get(k)==v for k,v in identity.items()),'every receipt belongs to admitted current experiment/source/plan')
 need(set(c)==ef|{'status','state_kind'}and c['status']=='SEALED'and c['state_kind']==state and digest({k:c[k]for k in ef})==digest(expected_state),'exact local complete-state lineage/cursor')
 need(set(r)==set(identity)|{'status','checkpoint_sha256','artifact_sha256','raw_teacher_included','frozen_dependencies_included'}and r['status']=='DOWNLOADED_EXACT'and r['checkpoint_sha256']==expected_state['checkpoint_sha256']and h64(r['artifact_sha256'])and r['raw_teacher_included']is False and r['frozen_dependencies_included']is False,'minimal current backup actual readback')
 need(set(f)==ef|{'status'}and f['status']=='FRESH_PROCESS_RESTORED'and digest({k:f[k]for k in ef})==digest(expected_state),'exact fresh-process current complete-state lineage/cursor')
 need(type(expected_generation)is int and expected_generation>=0 and h64(expected_head_sha),'previous verified head')
 need(set(h)==set(identity)|{'status','previous_generation','generation','previous_head_sha256','checkpoint_sha256','artifact_sha256','new_head_sha256','cursor','parent_checkpoint_sha256'},'exact head transition schema')
 need(h['status']=='CAS_READBACK_VERIFIED'and type(h['previous_generation'])is int and type(h['generation'])is int and h['previous_generation']==expected_generation and h['generation']==expected_generation+1 and h['previous_head_sha256']==expected_head_sha and h['checkpoint_sha256']==expected_state['checkpoint_sha256']and h['artifact_sha256']==r['artifact_sha256']and h64(h['new_head_sha256'])and digest(h['cursor'])==digest(cursor) and h['parent_checkpoint_sha256']==parent,'expected generation CAS and exact current-state head readback')
 return {'next_update_ready':True,'checkpoint_sha256':c['checkpoint_sha256'],'head_generation':h['generation'],'head_sha256':h['new_head_sha256']}
