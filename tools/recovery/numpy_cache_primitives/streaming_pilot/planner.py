"""Pure finite one-scene planner; trusted loader/provenance admission is separate."""
import hashlib,json

def need(v,m):
 if not v:raise ValueError(m)
def canonical(x):return json.dumps(x,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def digest(x):return hashlib.sha256(canonical(x)).hexdigest()
def h64(s):return type(s)is str and len(s)==64 and s!='0'*64 and all(c in '0123456789abcdef'for c in s)
def input_id(prefix,tail):
 need(type(prefix)is list and type(tail)is list and prefix and prefix[0]==2,'BOS prefix')
 need(1<=len(tail)<=63 and len(prefix)+len(tail)<=64,'supported geometry')
 need(all(type(x)is int and 0<=x<8192 for x in prefix)and all(type(x)is int and 3<=x<8192 for x in tail),'supported exact tokens')
 return digest({'schema':'full-prefix-tail-input-v1','prefix':prefix,'tail':tail})
def cache_key(contract_sha,iid,prefix,tail):
 need(h64(contract_sha),'exact teacher contract');need(iid==input_id(prefix,tail),'input identity')
 # Exact existing NumPy cache-key schema; lineage is explicit, not a new math namespace.
 return digest({'schema':'numpy-teacher-numeric-key-v2','numeric_contract_sha256':contract_sha,'input_id':iid,'prefix':prefix,'tail':tail})
def scene_check(scene):
 need(set(scene)=={'scene_id','split','source_sha256','conditions'},'target-free scene schema only')
 need(type(scene['scene_id'])is str and scene['scene_id'] and scene['split']=='train' and h64(scene['source_sha256']),'TRAIN source identity')
 rows=scene['conditions'];need(type(rows)is list and len(rows)==4,'exact four pools')
 need({(r['mode'],r['condition'])for r in rows}=={('pureinitial','cold'),('pureinitial','given'),('mixed','cold'),('mixed','given')},'complete mode/condition cross')
 need(len({r['condition_id']for r in rows})==4,'unique condition ids')
 for row in rows:
  need(set(row)=={'condition_id','mode','condition','source_sha256','prefix','candidates'},'no gold/labels/scores in selection')
  need(h64(row['source_sha256'])and type(row['condition_id'])is str,'condition provenance')
  need(type(row['candidates'])is list and row['candidates'],'full nonempty pool')
  prefix=row['prefix'];need(type(prefix)is list and 1<=len(prefix)<=64 and prefix[0]==2 and all(type(x)is int and 0<=x<8192 for x in prefix),'complete original prefix')
  for i,c in enumerate(row['candidates']):
   need(set(c)=={'index','tail','supported','reasons','input_id'},'target-free candidate only')
   need(type(c['index'])is int and c['index']==i and type(c['supported'])is bool and type(c['reasons'])is list,'original pool ordering')
   tail=c['tail'];need(type(tail)is list and all(type(x)is int and 0<=x<8192 for x in tail),'original candidate token encoding')
   reasons=[]
   if not 1<=len(tail)<=63:reasons.append('candidate_scalar_length_outside_1_63')
   if any(x<3 for x in tail):reasons.append('candidate_OOV')
   if len(prefix)+len(tail)>64:reasons.append('context_plus_candidate_exceeds_64')
   need(c['reasons']==reasons and c['supported']==(not reasons),'support selection rederived, not trusted caller flag')
   need(c['input_id']==(input_id(prefix,tail)if c['supported']else None),'existing canonical input_id matches exact unchanged token arrays')
 return all(c['supported']and not c['reasons']for r in rows for c in r['candidates'])
def plan(train_scenes,contract_sha,sentinel,*,unique_per_chunk=32):
 need(type(sentinel)is dict and set(sentinel)=={'prefix','tail'},'exact target-free sentinel schema; no labels/scores/extra fields')
 need(type(train_scenes)is list and train_scenes,'TRAIN-only manifest required')
 need(type(unique_per_chunk)is int and 1<=unique_per_chunk<=32,'at most32 unique+2sentinel=34 calls')
 need(len({s['scene_id']for s in train_scenes})==len(train_scenes),'unique scene source ids')
 eligible=[s for s in train_scenes if scene_check(s)];need(eligible,'complete supported TRAIN scene exists')
 chosen=min(eligible,key=lambda s:s['scene_id']);rows=chosen['conditions'];inputs={};mapped=[]
 for row in rows:
  mapping=[]
  for c in row['candidates']:
   iid=input_id(row['prefix'],c['tail']);value={'input_id':iid,'prefix':list(row['prefix']),'tail':list(c['tail'])};value['cache_key']=cache_key(contract_sha,iid,value['prefix'],value['tail'])
   need(iid not in inputs or inputs[iid]==value,'no identity collision');inputs[iid]=value;mapping.append({'index':c['index'],'input_id':iid,'cache_key':value['cache_key']})
  mapped.append({'condition_id':row['condition_id'],'mode':row['mode'],'condition':row['condition'],'source_sha256':row['source_sha256'],'weight_numerator':1,'weight_denominator':4,'candidates':mapping})
 sid=input_id(sentinel['prefix'],sentinel['tail']);need(sid not in inputs,'fixed qualified sentinel distinct from scene')
 ordered=sorted(inputs);chunks=[]
 for start in range(0,len(ordered),unique_per_chunk):
  ids=ordered[start:start+unique_per_chunk];ops=[{'kind':'sentinel_before','input_id':sid}]+[{'kind':'scene_teacher','input_id':i}for i in ids]+[{'kind':'sentinel_after','input_id':sid}]
  chunks.append({'index':len(chunks),'input_ids':ids,'operations':[dict(o,index=i)for i,o in enumerate(ops)],'score_calls':len(ids)+2,'load_calls':1,'getter_calls':2,'guard_checkpoints':2*(len(ids)+2)+6,'mapping_snapshots':2*(2*(len(ids)+2)+6)})
 value={'schema':'one-train-scene-streaming-plan-v1','purpose':'pipeline_correctness_not_quality','scene_count':1,'selected_scene_id':chosen['scene_id'],'scene_source_sha256':chosen['source_sha256'],'input_train_scenes':len(train_scenes),'eligible_train_scenes':len(eligible),'selection':'lexicographic first scene_id among complete four-pool supported TRAIN scenes; no label/score access','teacher_contract_sha256':contract_sha,'teacher_namespace_policy':'inherit exact qualified fullNumPy numeric contract/cache-key bytes only; distinct experiment/operation IDs; no TorchA/suffix mixing','conditions':mapped,'inputs':[inputs[i]for i in ordered],'sentinel':{'prefix':list(sentinel['prefix']),'tail':list(sentinel['tail']),'input_id':sid},'chunks':chunks,'total_initial_score_calls':sum(c['score_calls']for c in chunks),'total_initial_load_calls':len(chunks),'total_initial_getter_calls':2*len(chunks),'student_updates':1,'epochs':1,'execution_admission':False}
 value['plan_sha256']=digest(value);return value
