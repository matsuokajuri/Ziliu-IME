"""Pure durable-event classifier + reviewed zero-model recovery writer.
No scorer callback, process launching, deletion, remote IO, automatic retry.
Journal authentication/durable-readback and owned-process observation are caller
obligations; this core validates exact canonical hash chains, not disk durability.
"""
import hashlib,json
import cache_store_numpy_v2 as cache
def need(v,m):
 if not v:raise ValueError(m)
def digest(v):return hashlib.sha256(cache.canonical(v)).hexdigest()
def spec_hash(s):
 need(type(s)is dict and set(s)=={'schema','contract_sha256','window','attempt','keys','mode'},'exact window spec')
 need(s['schema']=='numpy-cache-window-v2' and cache.hex64(s['contract_sha256']) and s['contract_sha256']!='0'*64,'fixed schema/contract')
 cache.origin_ok({'window':s['window'],'attempt':s['attempt']});need(s['mode'] in ('score','cache_only'),'explicit mode')
 need(type(s['keys'])is list and s['keys'],'nonempty ordered keys');seen=set()
 for row in s['keys']:
  need(type(row)is dict and set(row)=={'cache_key','input_id'} and all(cache.hex64(x) for x in row.values()),'exact identities')
  need(row['cache_key'] not in seen,'duplicate key');seen.add(row['cache_key'])
 need([r['cache_key'] for r in s['keys']]==sorted(seen),'immutable sorted range');return digest(s)
def event(s,events,kind,data):
 """Build one synthetic/caller-persisted event, does not perform external work."""
 sh=spec_hash(s);p={'sequence':len(events),'spec_sha256':sh,'previous_sha256':digest(events[-1]) if events else None,'kind':kind,'data':data}
 return p

def inspect(s,events,*,process_state='UNKNOWN',source_integrity_verified=False):
 sh=spec_hash(s);need(process_state in ('UNKNOWN','NOT_LAUNCHED','EXITED'),'explicit process observation')
 need(type(source_integrity_verified)is bool and type(events)is list,'typed evidence')
 pos=0;state='EMPTY';completed=None;issued=0;saved=0;hits=0
 for i,e in enumerate(events):
  need(type(e)is dict and set(e)=={'sequence','spec_sha256','previous_sha256','kind','data'} and type(e['sequence'])is int and e['sequence']==i and e['spec_sha256']==sh and e['previous_sha256']==(digest(events[i-1]) if i else None),'exact event chain')
  kind,data=e['kind'],e['data']
  if i==0:need(kind=='intent' and data=={} ,'intent first');state='READY';continue
  if kind=='manifest':
   need(state=='READY' and pos==len(s['keys']) and cache.canonical(data)==cache.canonical({'saved':saved,'hits':hits,'issued':issued}),'complete exact manifest');state='COMPLETE';continue
  need(state!='COMPLETE' and pos<len(s['keys']),'no excess request or postmanifest event')
  row=s['keys'][pos]
  if kind=='issued':
   need(s['mode']=='score' and state=='READY' and data==row,'one exact issue before callback');state='ISSUED';issued+=1
  elif kind in ('completed','hit'):
   need(state==('ISSUED' if kind=='completed' else 'READY'),'ordered completion or read-only hit')
   cache.validate(data,s['contract_sha256'],row['cache_key'],row['input_id'])
   if kind=='completed':need(data['origin']=={'window':s['window'],'attempt':s['attempt']},'original request provenance');completed=data;state='COMPLETED'
   else:hits+=1;pos+=1;state='READY'
  elif kind=='saved':
   need(state=='COMPLETED' and data=={'payload_sha256':completed['payload_sha256']},'save only exact durable completed bits');saved+=1;pos+=1;state='READY';completed=None
  else:raise ValueError('unknown event')
 if state=='ISSUED':status='UNKNOWN_ISSUED_WITHOUT_COMPLETION'
 elif state=='COMPLETED':status='DURABLE_COMPLETED_REQUIRES_REVIEWED_ZERO_MODEL_RECOVERY'
 elif state=='COMPLETE':status='COMPLETE_REQUIRES_FRESH_CACHE_ONLY_VERIFICATION'
 elif issued==0 and hits==0 and process_state in ('NOT_LAUNCHED','EXITED') and source_integrity_verified:status='NUMERIC_NOT_STARTED_NEW_ATTEMPT_REQUIRES_REVIEW'
 else:status='INCOMPLETE_REQUIRES_RECONCILIATION'
 return {'status':status,'state':state,'spec_sha256':sh,'events_sha256':digest(events),'next_index':pos,'issued':issued,'saved':saved,'hits':hits,'unknown_score_upper_bound':int(state=='ISSUED'),'durable_completed_payload':completed,'automatic_retry':False,'model_calls_authorized':0,'journal_disk_durability_verified_here':False}

def recovery_plan(s,events,verified_disk_evidence_sha256):
 need(cache.hex64(verified_disk_evidence_sha256) and verified_disk_evidence_sha256!='0'*64,'external actual durable-readback evidence pin')
 r=inspect(s,events);need(r['state']=='COMPLETED','only durable completed score can be recovered without model')
 return {'schema':'numpy-zero-model-recovery-v2','spec_sha256':r['spec_sha256'],'events_sha256':r['events_sha256'],'verified_disk_evidence_sha256':verified_disk_evidence_sha256,'payload':r['durable_completed_payload'],'model_calls':0,'operation':'publish_original_completed_payload_only'}
def recover_durable_completed(store,plan,review):
 need(type(plan)is dict and set(plan)=={'schema','spec_sha256','events_sha256','verified_disk_evidence_sha256','payload','model_calls','operation'},'exact recovery plan')
 need(plan['schema']=='numpy-zero-model-recovery-v2' and type(plan['model_calls'])is int and plan['model_calls']==0 and plan['operation']=='publish_original_completed_payload_only','zero-model exact recovery')
 need(all(cache.hex64(plan[k]) and plan[k]!='0'*64 for k in ('spec_sha256','events_sha256','verified_disk_evidence_sha256')),'pinned recovery lineage')
 need(type(review)is dict and set(review)=={'approved','plan_sha256'} and review['approved'] is True and review['plan_sha256']==digest(plan),'independent exact recovery review')
 p=plan['payload'];cache.validate(p,p['contract_sha256'],p['cache_key'],p['input_id'])
 return store.publish(p['contract_sha256'],p['cache_key'],p['input_id'],p['value'],p['origin'])
