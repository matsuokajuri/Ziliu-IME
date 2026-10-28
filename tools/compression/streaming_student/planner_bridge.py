"""Target-free planner/closed labels -> candidate student scope, no E loader.
The candidate cannot pass the update gate until independent materialization review.
"""
import copy,struct
from state_codec import need,digest,sha
from label_contract import validate_scope,validate_labels

def materialize_candidate(plan,expected_plan_sha,transactions,score_records,expected_score_shas,records,expected_payload_shas,backend,provenance):
    unsigned={k:v for k,v in plan.items()if k!='plan_sha256'}
    need(plan.get('plan_sha256')==expected_plan_sha==digest(unsigned),'sealed immutable planner output')
    need(plan['schema']=='one-train-scene-streaming-plan-v1' and plan['purpose']=='pipeline_correctness_not_quality' and plan['scene_count']==plan['student_updates']==plan['epochs']==1,'one-scene one-update contract')
    closed=transactions.labels_complete(plan,score_records,expected_score_shas,expected_plan_sha=expected_plan_sha)
    need(closed['student_update_authorized']is False and closed['plan_sha256']==expected_plan_sha,'planner labels do not grant execution')
    need(set(provenance)=={'teacher_identity_sha256','split_manifest_sha256','final_exclusion_policy_sha256','frozen_embeddings_sha256','embedding_binding_sha256','teacher_domain','source_identity_sha256'},'independently reviewed materializer provenance')
    for k,v in provenance.items():need(type(v)is str and bool(v) if k=='teacher_domain' else sha(v),'exact provenance')
    ids=[r['condition_id']for r in plan['conditions']]
    need(type(records)is list and [r['id']for r in records]==ids and set(expected_payload_shas)==set(ids),'exact ordered four record payloads')
    need(all(sha(v)for v in expected_payload_shas.values()),'external expected materialized payload hashes')
    byid={r['input_id']:r for r in score_records};scope_rows=[];proof_rows=[]
    for row,lab,r in zip(plan['conditions'],closed['conditions'],records):
        need(row['condition_id']==lab['condition_id']==r['id'] and row['weight_numerator']==lab['weight_numerator']==1 and row['weight_denominator']==lab['weight_denominator']==4,'exact four-condition mapping')
        candidates=row['candidates'];need([c['index']for c in candidates]==list(range(len(candidates))),'full original candidate ordering')
        need(len(lab['candidates'])==len(candidates) and [c['input_id']for c in candidates]==[c['input_id']for c in lab['candidates']],'all aliases/candidates retained')
        need(backend.input_digest(r)==expected_payload_shas[r['id']],'materialized embedding/target bytes match external seal')
        tag=row['mode']+'/'+row['condition'];s={'id':r['id'],'condition':tag,'candidate_keys':[c['cache_key']for c in candidates],'candidate_input_sha256s':[c['input_id']for c in candidates],'payload_sha256':expected_payload_shas[r['id']]};scope_rows.append(s)
        proof_rows.append({**s,'scene':plan['selected_scene_id'],'split':'TRAIN','score_fp32_hex':[struct.pack('<f',byid[c['input_id']]['value']).hex()for c in candidates],'score_evidence_sha256s':[expected_score_shas[c['input_id']]for c in candidates]})
    scope={'data_namespace':'REAL_TRAIN_V1','teacher_namespace':'FULL_NUMPY_R59_QUALIFIED_V1','schema':'ONE_COMPLETE_TRAIN_SCENE_V1','scene':plan['selected_scene_id'],'split':'TRAIN','conditions':[r['condition']for r in scope_rows],'records':scope_rows,'teacher_contract_sha256':plan['teacher_contract_sha256'],'max_student_updates':1,'selection_manifest_sha256':expected_plan_sha,**{k:v for k,v in provenance.items()if k!='source_identity_sha256'}}
    sh=digest(scope);validate_scope(scope,sh)
    label={'scope_sha256':sh,'data_namespace':scope['data_namespace'],'teacher_namespace':scope['teacher_namespace'],**{k:scope[k]for k in ('teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256')},'status':'PENDING_MATERIALIZATION_REVIEW','unknown_requests':0,'records':proof_rows}
    # Validate the projected closed form without claiming an independent review.
    projected=copy.deepcopy(label);projected['status']='INDEPENDENTLY_CLOSED_COMPLETE';validate_labels(scope,sh,projected,digest(projected),records,backend)
    return {'scope':scope,'labels':label,'materialized_record_manifest_sha256':digest(expected_payload_shas),'source_identity_sha256':provenance['source_identity_sha256'],'student_update_authorized':False}

def admit_materialization(candidate,candidate_seal,review,review_seal):
    need(digest(candidate)==candidate_seal and digest(review)==review_seal,'separate sealed materialization review')
    need(set(candidate)=={'scope','labels','materialized_record_manifest_sha256','source_identity_sha256','student_update_authorized'} and candidate['student_update_authorized']is False,'candidate is not an execution grant')
    need(set(review)=={'status','candidate_sha256','scope_sha256','closed_labels_sha256','materialized_record_manifest_sha256','source_identity_sha256'},'exact independent review')
    need(review['status']=='MATERIALIZATION_INDEPENDENTLY_VERIFIED' and review['candidate_sha256']==candidate_seal and review['scope_sha256']==digest(candidate['scope']),'independent review identity')
    for k in ('materialized_record_manifest_sha256','source_identity_sha256'):need(review[k]==candidate[k] and sha(review[k]),'materialization source/payload closure')
    need(candidate['labels']['status']=='PENDING_MATERIALIZATION_REVIEW','one immutable candidate stage')
    out=copy.deepcopy(candidate['labels']);out['status']='INDEPENDENTLY_CLOSED_COMPLETE'
    need(digest(out)==review['closed_labels_sha256'],'exact reviewed closed labels')
    return out

def backup_proof_from_planner(transactions,state_kind,receipts,receipt_seals,loop,contract_sha,source_identity_sha,expected_generation,expected_head_sha,expected_state,expected_plan_sha,expected_parent_checkpoint_sha):
    need(sha(contract_sha) and sha(source_identity_sha) and sha(expected_plan_sha),'trusted runtime/plan/source anchors')
    step=loop.state['cursor']['completed_updates']
    need(type(step)is int and step in (0,1) and loop.state['cursor']['next_batch']==step,'one-scene exact local cursor')
    derived_cursor={'epoch':step,'scene_index':0,'condition_index':0,'completed_updates':step}
    need(expected_state['experiment_sha256']==contract_sha and expected_state['source_identity_sha256']==source_identity_sha and expected_state['plan_sha256']==expected_plan_sha,'trusted expected experiment/source/plan, not receipt self-selection')
    need(expected_state['parent_checkpoint_sha256']==expected_parent_checkpoint_sha and expected_state['state_schema']==loop.identity['schema_sha256'],'trusted parent and checkpoint schema')
    need(expected_state['checkpoint_sha256']==loop.tip and expected_state['complete_state_sha256']==digest(loop.state) and expected_state['cursor']==derived_cursor,'actual exact state and derived one-scene streaming cursor')
    ready=transactions.next_update_gate(state_kind,receipts,receipt_seals,expected_generation=expected_generation,expected_head_sha=expected_head_sha,expected_state=expected_state)
    c,r,f,h=(receipts[k]for k in ('checkpoint','remote','restore','head'))
    need(c['source_identity_sha256']==source_identity_sha and sha(source_identity_sha),'exact reviewed full execution source identity')
    need(c['checkpoint_sha256']==loop.tip and c['complete_state_sha256']==digest(loop.state),'actual loop step/cursor/state bytes, not a different checkpoint')
    need(state_kind==('GENESIS_SEALED'if loop.state['cursor']['completed_updates']==0 else 'UPDATED_LOCAL_SEALED'),'step/state-kind agreement')
    need(ready['next_update_ready']is True,'planner remote gate complete')
    # Still a candidate proof: its digest must be sealed by independent coordinator.
    return {'status':'REMOTE_READBACK_FRESH_RESTORE_HEAD_CAS_VERIFIED','contract_sha256':contract_sha,'checkpoint_sha256':loop.tip,'state_sha256':digest(loop.state),'step':loop.state['cursor']['completed_updates'],'archive_sha256':r['artifact_sha256'],'remote_readback_archive_sha256':r['artifact_sha256'],'fresh_restore_state_sha256':f['complete_state_sha256'],'head_before_generation':expected_generation,'head_after_generation':ready['head_generation'],'previous_head_sha256':expected_head_sha,'head_sha256':ready['head_sha256'],'head_readback_sha256':ready['head_sha256'],'unknown_operations':0}
