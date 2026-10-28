"""Synthetic-testable tiny-pilot adapter; no runtime import or automatic execution.
Real callers must separately pin the qualified full NumPy scorer/loader/runtime.
One journal and one staging store per operation, including repeats/sentinels.
"""
import hashlib,json
import contract_numpy_v2 as contract
import cache_store_numpy_v2 as cache
import recovery_protocol_v2 as protocol

def need(v,m):
 if not v:raise ValueError(m)
def full_contract(c):
 ch=contract.validate_contract(c);need(c['implementation']=='full_projection','suffix rejected for this selected baseline');return ch

def tiny_schedule(maxima,sentinel_input_id):
 """Qualified metadata maxima only; never choose based on scores or labels."""
 need(type(maxima)is dict and set(maxima)=={'forward_length','prefix_length','tail_length'},'exact geometry objectives')
 need(contract.hash64(sentinel_input_id),'qualified fixed sentinel identity')
 chosen=[]
 for name in ('forward_length','prefix_length','tail_length'):
  r=maxima[name];need(type(r)is dict and set(r)=={'input_id','value'} and contract.hash64(r['input_id']) and type(r['value'])is int and r['value']>0,'exact token-only maximum')
  if r['input_id'] not in chosen:chosen.append(r['input_id'])
 need(sentinel_input_id not in chosen,'sentinel separate from selected worst inputs')
 ops=[{'kind':'sentinel_before','input_id':sentinel_input_id}]
 ops += [{'kind':'geometry_primary','input_id':iid} for iid in chosen]
 ops += [{'kind':'geometry_repeat','input_id':iid} for iid in chosen]
 ops += [{'kind':'sentinel_after','input_id':sentinel_input_id}]
 return {'schema':'FULL_NUMPY_TINY_GEOMETRY_PILOT_SCHEDULE_V1','operations':[dict(x,index=i) for i,x in enumerate(ops)],'unique_geometry_inputs':len(chosen),'score_calls':2*len(chosen)+2,'sentinel_calls':2,'repeat_calls':len(chosen),'model_loads':1,'native_thread_getters':2,'production_cache_entries_published':0,'production_window_size_frozen':False}

def operation_spec(c,binding,index,*,window=0):
 ch=full_contract(c);need(type(index)is int and 0<=index<8,'bounded pilot operation index');need(type(binding)is dict and set(binding)=={'cache_key','input_id'} and all(contract.hash64(v) for v in binding.values()),'exact numerical binding')
 return {'schema':'numpy-cache-window-v2','contract_sha256':ch,'window':window,'attempt':f'full-numpy-tiny-pilot-v1-op-{index:03d}','keys':[dict(binding)],'mode':'score'}

def perform_one(journal,store,score_callback,check_before,check_after,storage_gate):
 """One admitted callback, once. Exceptions propagate; no retries/hit fallback.

 Caller owns isolated per-operation roots and external serial lease. Returned
 records are staging evidence only, not a full production-window manifest.
 Runtime checkpoints must be losslessly persisted by the callbacks before return.
 """
 spec=journal.spec;need(len(spec['keys'])==1 and spec['mode']=='score','one scoring operation only')
 state=journal.classify(process_state='UNKNOWN',source_integrity_verified=False)['classification']
 need(state['state']=='EMPTY','new exact operation, never replay old journal')
 row=spec['keys'][0];journal.append('intent',{})
 # Before-guard failure proves no callback issued, but still needs reconciliation.
 check_before()
 storage=storage_gate();need(type(storage)is dict and storage.get('status')=='STORAGE_ADMITTED' and storage.get('new_score_allowed') is True,'fresh storage/inode gate before issue')
 journal.append('issued',row)  # fsync, commit watermark and full readback first.
 value=score_callback()
 p=cache.payload(spec['contract_sha256'],row['cache_key'],row['input_id'],value,{'window':spec['window'],'attempt':spec['attempt']})
 journal.append('completed',p) # durable exact result before any post-score guard.
 check_after()
 actual=store.publish(p['contract_sha256'],p['cache_key'],p['input_id'],p['value'],p['origin'])
 need(cache.canonical(actual)==cache.canonical(p),'exact staging payload')
 journal.append('saved',{'payload_sha256':p['payload_sha256']});journal.append('manifest',{'saved':1,'hits':0,'issued':1})
 return {'payload':p,'state':journal.classify()['classification'],'model_calls':1,'production_entry':False}

def check_repeat_bits(schedule,results,expected_sentinel_bits):
 need(type(results)is list and len(results)==schedule['score_calls'] and type(expected_sentinel_bits)is str and len(expected_sentinel_bits)==8,'complete pilot observations')
 first={};repeat=0
 for op,result in zip(schedule['operations'],results):
  need(result['input_id']==op['input_id'] and result['index']==op['index'],'fixed operation order')
  bits=cache.fp32_bits(result['value'])
  if op['kind'].startswith('sentinel_'):need(bits==expected_sentinel_bits,'qualified full-NumPy sentinel bits')
  elif op['kind']=='geometry_primary':need(op['input_id']not in first,'unique primary');first[op['input_id']]=bits
  else:need(op['kind']=='geometry_repeat' and first[op['input_id']]==bits,'repeat must be exact full-NumPy bits');repeat+=1
 need(repeat==schedule['repeat_calls'],'all repeat calls accounted');return {'sentinel_calls':2,'repeat_calls':repeat,'all_bits_equal':True,'reference_is_original_torch_A':False,'model_calls_performed_here':0}

def train_only_occurrences(worklist,condition_ids):
 """Upstream must independently pin full worklist and verify all cache readbacks.
 Returns existing rows; never reads labels, inserts gold, filters candidates, or
 grants training access to DEV. All unsupported candidates remain present.
 """
 need(type(condition_ids)is list and len(condition_ids)==len(set(condition_ids)),'unique requested TRAIN conditions')
 rows={r['condition_id']:r for r in worklist['occurrences']};need(len(rows)==len(worklist['occurrences']),'unique full scope')
 need(all(cid in rows and rows[cid]['split']=='train' for cid in condition_ids),'TRAIN only; DEV forbidden')
 return [rows[cid] for cid in condition_ids]
