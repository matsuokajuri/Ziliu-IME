"""Structural proof gate for independently SEALED TRAIN label materialization.
Does not generate teacher scores or establish external provenance by itself.
"""
import math,struct
from state_codec import need,digest,sha

SCOPE_FIELDS={'schema','scene','split','conditions','records','teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256','max_student_updates','selection_manifest_sha256','split_manifest_sha256','final_exclusion_policy_sha256','teacher_domain','data_namespace','teacher_namespace'}
LABEL_FIELDS={'scope_sha256','teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256','status','unknown_requests','records','data_namespace','teacher_namespace'}

def validate_scope(scope,expected_sha):
    need(digest(scope)==expected_sha and set(scope)==SCOPE_FIELDS,'externally sealed complete scope')
    need(scope['data_namespace'] in ('REAL_TRAIN_V1','SYNTHETIC_QUALIFICATION_V1'),'explicit allowed data namespace')
    expected_teacher='FULL_NUMPY_R59_QUALIFIED_V1' if scope['data_namespace']=='REAL_TRAIN_V1' else 'SYNTHETIC_FIXED_TARGETS_V1'
    need(scope['teacher_namespace']==expected_teacher,'synthetic targets cannot masquerade as real teacher')
    need(scope['schema']=='ONE_COMPLETE_TRAIN_SCENE_V1' and scope['split']=='TRAIN','TRAIN only')
    need(type(scope['teacher_domain'])is str and scope['teacher_domain'],'explicit teacher seen-domain boundary')
    for k in ('selection_manifest_sha256','split_manifest_sha256','final_exclusion_policy_sha256'):need(sha(scope[k]),'frozen selection/split/final exclusion source provenance')
    need(type(scope['scene'])is str and scope['scene'] and type(scope['conditions'])is list and len(scope['conditions'])==4 and len(set(scope['conditions']))==4 and all(type(x)is str and x for x in scope['conditions']),'one scene/four condition names')
    need(type(scope['max_student_updates'])is int and scope['max_student_updates']==1,'finite first pipeline update only')
    for k in ('teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256'):need(sha(scope[k]),'teacher/frozen embedding provenance pins')
    need(type(scope['records'])is list and len(scope['records'])==4,'four full condition pools')
    ids=[];conditions=[]
    for r in scope['records']:
        need(set(r)=={'id','condition','candidate_keys','candidate_input_sha256s','payload_sha256'},'scope record schema')
        need(type(r['id'])is str and r['id'] and r['condition'] in scope['conditions'],'frozen record id/condition')
        keys=r['candidate_keys'];inputs=r['candidate_input_sha256s']
        need(type(keys)is list and len(keys)>0 and type(inputs)is list and len(inputs)==len(keys) and all(sha(x)for x in keys+inputs),'complete candidate mappings')
        need(sha(r['payload_sha256']),'all embeddings/teacher/gold exact payload pin')
        ids.append(r['id']);conditions.append(r['condition'])
    need(len(set(ids))==4 and conditions==scope['conditions'],'fixed complete condition order')
    return scope

def fp32(raw):
    need(type(raw)is str and len(raw)==8 and all(c in '0123456789abcdef'for c in raw),'exact little-endian FP32 hex')
    v=struct.unpack('<f',bytes.fromhex(raw))[0];need(math.isfinite(v) and v<=0,'finite nonpositive teacher logprob')
    return v

def validate_labels(scope,scope_sha,labels,sealed_labels_sha,records,backend):
    validate_scope(scope,scope_sha)
    need(digest(labels)==sealed_labels_sha and set(labels)==LABEL_FIELDS,'independently sealed labels, exact schema')
    need(labels['scope_sha256']==scope_sha and labels['status']=='INDEPENDENTLY_CLOSED_COMPLETE' and type(labels['unknown_requests'])is int and labels['unknown_requests']==0,'no unknown, partial or merely staged labels')
    for k in ('teacher_identity_sha256','teacher_contract_sha256','frozen_embeddings_sha256','embedding_binding_sha256','data_namespace','teacher_namespace'):need(labels[k]==scope[k],'exact teacher/E identities')
    need(type(labels['records'])is list and len(labels['records'])==4 and type(records)is list and len(records)==4,'one complete four-pool scene')
    observed={}
    for spec,proof,r in zip(scope['records'],labels['records'],records):
        need(set(proof)=={'id','scene','split','condition','candidate_keys','candidate_input_sha256s','score_fp32_hex','score_evidence_sha256s','payload_sha256'},'exact label proof record')
        need(proof['scene']==scope['scene'] and proof['split']=='TRAIN' and proof['id']==spec['id'] and proof['condition']==spec['condition'],'only frozen TRAIN provenance')
        for key in ('candidate_keys','candidate_input_sha256s','payload_sha256'):need(proof[key]==spec[key],'full ordered pool; no filtering/reordering')
        n=len(spec['candidate_keys']);bits=proof['score_fp32_hex'];evidence=proof['score_evidence_sha256s']
        need(type(bits)is list and len(bits)==n and type(evidence)is list and len(evidence)==n and all(sha(x)for x in evidence),'complete candidate score evidence')
        need(set(r)=={'id','context','candidates','teacher_scores','gold_index'} and r['id']==spec['id'],'payload schema')
        need(type(r['candidates'])is list and len(r['candidates'])==n and type(r['teacher_scores'])is list and len(r['teacher_scores'])==n,'complete actual pool')
        need(r['gold_index'] is None or(type(r['gold_index'])is int and 0<=r['gold_index']<n),'gold provenance is inside sealed payload; no coercion')
        for key,inp,b,v in zip(spec['candidate_keys'],spec['candidate_input_sha256s'],bits,r['teacher_scores']):
            f=fp32(b);need(type(v)is float and math.isfinite(v) and struct.pack('<f',v).hex()==b and v==float(f),'exact FP32 value not merely rounded FP64')
            if key in observed:need(observed[key]==(inp,b),'shared cache key must bind same input/value')
            observed[key]=(inp,b)
        need(backend.input_digest(r)==proof['payload_sha256'],'actual complete embedding/label/gold bytes')
    return True
