"""One-update gate around reviewed TrainingLoop; never calls remote services.
Trusted coordinator supplies independently verified, sealed remote evidence.
Flags alone are not proof of network durability; launch admission binds seals.
"""
import copy
from state_codec import need,digest,sha
from label_contract import validate_scope,validate_labels

BACKUP_FIELDS={'status','contract_sha256','checkpoint_sha256','state_sha256','step','archive_sha256','remote_readback_archive_sha256','fresh_restore_state_sha256','head_before_generation','head_after_generation','head_sha256','head_readback_sha256','unknown_operations','previous_head_sha256'}

def validate_backup(proof,seal,contract_sha,tip,state,expected_generation,expected_head_sha):
    need(sha(expected_head_sha) and sha(seal) and sha(contract_sha) and sha(tip),'sealed backup identity hashes')
    need(digest(proof)==seal and set(proof)==BACKUP_FIELDS,'independent sealed backup proof')
    need(proof['status']=='REMOTE_READBACK_FRESH_RESTORE_HEAD_CAS_VERIFIED','not uploaded-only, local-only or pending backup')
    need(proof['contract_sha256']==contract_sha and proof['checkpoint_sha256']==tip,'actual contract/checkpoint')
    state_sha=digest(state);need(proof['state_sha256']==proof['fresh_restore_state_sha256']==state_sha,'complete restored p/m/v/RNG/cursor')
    need(type(proof['step'])is int and proof['step']==state['cursor']['completed_updates'],'actual current step')
    need(type(proof['unknown_operations'])is int and proof['unknown_operations']==0,'backup/head UNKNOWN blocks NEXT_READY')
    for k in ('archive_sha256','remote_readback_archive_sha256','head_sha256','head_readback_sha256'):need(sha(proof[k]),'exact remote evidence hashes')
    need(proof['previous_head_sha256']==expected_head_sha,'expected previous head bytes, not generation alone')
    need(proof['archive_sha256']==proof['remote_readback_archive_sha256'] and proof['head_sha256']==proof['head_readback_sha256'],'actual remote readback equal')
    need(type(expected_generation)is int and expected_generation>=0 and type(proof['head_before_generation'])is int and type(proof['head_after_generation'])is int and proof['head_before_generation']==expected_generation and proof['head_after_generation']==expected_generation+1,'head expected generation/CAS')
    return proof['head_after_generation']

class _AnchoredBackend:
    """Post-callback anchor check at the loop's final payload validation boundary.

    The reviewed loop invokes input_digest before publishing issued. Check after
    *every* delegated digest, not only after the session's earlier label check.
    This is an in-process contract guard, not isolation from malicious Python.
    """
    def __init__(self, backend, check):
        self.backend=backend;self.check=check
    def input_digest(self, record):
        result=self.backend.input_digest(record)
        self.check()
        return result
    def train(self, state, records, weight, identity):
        self.check()
        return self.backend.train(state,records,weight,identity)
    def validate_state(self, state, identity):
        return self.backend.validate_state(state,identity)

class OneSceneSession:
    def __init__(self,loop,scope,scope_sha,contract_sha,expected_head_generation,expected_head_sha,expected_namespace):
        validate_scope(scope,scope_sha);need(expected_namespace in ('REAL_TRAIN_V1','SYNTHETIC_QUALIFICATION_V1') and scope['data_namespace']==expected_namespace,'namespace fixed by admitted caller, not payload')
        need(sha(contract_sha),'external exact runtime/study contract')
        need(loop.state is not None and not loop.unknown and not loop.closed,'already initialized/restored reviewed loop')
        need(type(expected_head_generation)is int and expected_head_generation>=0 and sha(expected_head_sha),'expected head generation and bytes')
        need(loop.identity['conditions']==scope['conditions'] and loop.policy['teacher_domain']==scope['teacher_domain'],'condition schema and teacher-domain provenance')
        ids=[r['id']for r in scope['records']]
        need(loop.policy['train_batches']==[ids],'exactly one frozen scene update schedule')
        for r in scope['records']:
            p=loop.policy['records'][r['id']]
            need(p['split']=='TRAIN' and p['scene']==scope['scene'] and p['condition']==r['condition'] and p['payload_sha256']==r['payload_sha256'],'loop policy agrees with sealed full-pool scope')
        self.loop=loop;self.scope=copy.deepcopy(scope);self.scope_sha=scope_sha;self.contract_sha=contract_sha;self.generation=expected_head_generation;self.head_sha=expected_head_sha
        self.backup_needed=True;self.poisoned=False
        self._admitted_binding=self._binding()
        self._accepted_anchor=None
        self._backend=loop.backend
    def _binding(self):
        return (digest(self.loop.identity),digest(self.loop.policy),digest(self.scope),self.scope_sha,self.contract_sha)
    def _anchor(self):
        return (digest(self.loop.state),self.loop.tip,self._binding(),self.generation,self.head_sha)
    def _check_binding(self):
        need(not self.loop.closed and self._binding()==self._admitted_binding,'admitted source/policy/scope/contract changed')
    def _check_accepted(self):
        self._check_binding()
        need(self._accepted_anchor is not None and self._anchor()==self._accepted_anchor,'backed full state/tip/source/plan/contract/head changed')
    def accept_verified_backup(self,proof,independent_seal):
        need(not self.poisoned and not self.loop.unknown and self.backup_needed,'backup admission only when required and not uncertain')
        self._check_binding()
        self.generation=validate_backup(proof,independent_seal,self.contract_sha,self.loop.tip,self.loop.state,self.generation,self.head_sha)
        self.head_sha=proof['head_sha256'];self.backup_needed=False
        self._accepted_anchor=self._anchor()
    def update(self,records,labels,independent_label_seal):
        need(not self.poisoned and not self.loop.unknown and not self.backup_needed,'genesis/current checkpoint backup and head must close first')
        self._check_accepted()
        need(self.loop.backend is self._backend,'admitted backend replaced')
        need(self.loop.state['cursor']['completed_updates']==0,'one pilot update, no auto successor/epoch')
        validate_labels(self.scope,self.scope_sha,labels,independent_label_seal,records,self.loop.backend)
        self._check_accepted() # input-digest adapter must not mutate the backed loop
        need(self.loop.backend is self._backend,'admitted backend replaced by label callback')
        self.backup_needed=True;self.poisoned=True
        def checked():
            need(self.loop.backend is guarded,'guarded backend replaced during payload callback')
            self._check_accepted()
        guarded=_AnchoredBackend(self._backend,checked)
        self.loop.backend=guarded
        try:
            result=self.loop.update(records)
        finally:
            # Never reset a callback replacement silently. All failures retain
            # poisoned status and prohibit retries even if no issued exists.
            if self.loop.backend is guarded:self.loop.backend=self._backend
        self._accepted_anchor=None
        self.poisoned=False
        return result
    def status(self):
        if self.poisoned or self.loop.unknown:return 'UNKNOWN_NO_REPLAY'
        if self.backup_needed:return 'BACKUP_AND_HEAD_REQUIRED'
        self._check_accepted()
        return 'PILOT_COMPLETE' if self.loop.state['cursor']['completed_updates']==1 else 'GENESIS_BACKED_READY_ONE_UPDATE'
