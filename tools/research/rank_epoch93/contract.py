"""Independent fixed remaining93 ranking continuation from fork1, not old epoch."""
import hashlib,json,math
SCHEMA='PUBLIC_RANK_REMAINING93_IDENTITY_V1'
NAMESPACE='PUBLIC_BASE_RANK_FIXED97_COMPLETION_V1'
OBJECTIVE={'survival':'original_unchanged','regularizer':0.001,'ranking_margin':1.0,'ranking_weight':1.0,'gradient_clip':1.0}
OPTIMIZER={'name':'AdamW','learning_rate':0.001,'betas':[0.9,0.999],'epsilon':1e-8,'weight_decay':0.0}
def need(v,m):
 if not v:raise ValueError(m)
def digest(v):return hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
def h64(v):return type(v)is str and len(v)==64 and v!='0'*64 and all(c in'0123456789abcdef'for c in v)
def same_typed(v,e):
 if type(v)is not type(e):return False
 if type(e)is dict:return set(v)==set(e)and all(same_typed(v[k],e[k])for k in e)
 if type(e)is list:return len(v)==len(e)and all(same_typed(a,b)for a,b in zip(v,e))
 return v==e
def validate(i,parent_validate):
 need(type(i)is dict and set(i)=={'schema','namespace','parent_identity','parent_identity_sha256','parent_checkpoint_sha256','parent_rng_file_sha256','parent_state_sha256','parent_rng_state_sha256','schedule','source_sha256','selection_review_sha256','objective','optimizer','parent_adam_step','maximum_updates'},'closed remaining93 identity')
 need(i['schema']==SCHEMA and i['namespace']==NAMESPACE,'new independent continuation identity')
 p=i['parent_identity'];need(p['schema']=='PUBLIC_RANK_FORK_IDENTITY_V1','only original rank fork parent');done=parent_validate(p)
 old=p['parent_identity']['schedule'];need(len(old)==25 and len(old[0])==1 and all(len(b)==4 for b in old[1:]),'original 1+24x4 epoch schedule')
 flat=[x for b in old for x in b];need(len(flat)==len(set(flat))==97 and all(type(x)is str and x for x in flat),'exact unique original97')
 need(done==[old[2]] and len(old[2])==4,'fork already processed exact third batch')
 schedule=old[:2]+old[3:];need(i['schedule']==schedule and len(schedule)==24 and sum(map(len,schedule))==93,'remaining93, original order, completed four not replayed')
 need(set(x for b in schedule for x in b).isdisjoint(done[0]),'completed four excluded')
 need(i['parent_identity_sha256']==digest(p),'parent identity binding')
 for k in('parent_checkpoint_sha256','parent_rng_file_sha256','parent_state_sha256','parent_rng_state_sha256','source_sha256','selection_review_sha256'):need(h64(i[k]),'explicit exact provenance')
 need(type(i['parent_adam_step'])is int and i['parent_adam_step']==26 and type(i['maximum_updates'])is int and i['maximum_updates']==24,'24 updates after forkAdam26')
 need(same_typed(i['objective'],OBJECTIVE)and same_typed(i['optimizer'],OPTIMIZER),'unchanged ranking objective and optimizer')
 return schedule
def counters(i,u,parent_validate):
 schedule=validate(i,parent_validate);need(type(u)is int and 0<=u<=24,'new logical0..24')
 return {'logical_update':u,'adam_step':26+u,'scene_cursor':sum(map(len,schedule[:u]))}
def window(i,start,parent_validate):
 validate(i,parent_validate);need(type(start)is int and start in(0,4,8,12,16,20),'fixed window boundary only')
 return list(range(start,start+4))
def validate_history(history,u,i,parent_validate):
 schedule=validate(i,parent_validate);need(type(u)is int and 0<=u<=24 and type(history)is list and len(history)==u,'new continuation history only')
 for n,row in enumerate(history,1):
  need(type(row)is dict and set(row)=={'logical_update','adam_step','scenes','conditions','loss','gradient_norm','ranking_applied_conditions','ranking_ineligible_conditions'},'closed update record')
  scenes=len(schedule[n-1]);need(type(row['logical_update'])is int and row['logical_update']==n and type(row['adam_step'])is int and row['adam_step']==26+n and type(row['scenes'])is int and row['scenes']==scenes,'exact logical/global/batch counters')
  need(type(row['conditions'])is int and 2*scenes<=row['conditions']<=4*scenes and row['conditions']%2==0,'all declared modes and both contexts')
  need(all(type(row[k])in(float,int)and math.isfinite(row[k])and row[k]>=0 for k in('loss','gradient_norm')),'finite nonnegative stats')
  need(all(type(row[k])is int and row[k]>=0 for k in('ranking_applied_conditions','ranking_ineligible_conditions'))and row['ranking_applied_conditions']+row['ranking_ineligible_conditions']==row['conditions'],'exact ranking coverage')
