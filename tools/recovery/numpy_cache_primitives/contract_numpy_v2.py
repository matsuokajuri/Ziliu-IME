"""Pure NumPy-contract/key core. No IO, runtime, real binding or scorer selection."""
import hashlib,json,re
IMPLEMENTATIONS={'full_projection','suffix_projection'}
HASH_FIELDS={'scorer_sha256','loader_sha256','core_sha256','endpoint_sha256','base_sha256','tensor_representation_sha256','vocab_sha256','runtime_identity_sha256'}
FIELDS=HASH_FIELDS|{'schema','implementation','formula','precision','vocabulary_logits','bos','eos','openblas_threads','affinity_width','runtime_contract_schema'}
FORMULAS={arm:'numpy_fp32_erf_gelu_attention;'+arm+';tail_log_softmax8192;gather_tail;fp32_mean;full_prefix;noEOS' for arm in IMPLEMENTATIONS}
def need(v,m):
 if not v:raise ValueError(m)
def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode()
def hash64(v):return type(v)is str and re.fullmatch('[0-9a-f]{64}',v) is not None and v!='0'*64
def digest(v):return hashlib.sha256(canonical(v)).hexdigest()
def validate_contract(c):
 need(type(c)is dict and set(c)==FIELDS,'exact NumPy contract fields')
 need(all(hash64(c[k]) for k in HASH_FIELDS),'nonplaceholder source/runtime hashes')
 need(c['implementation'] in IMPLEMENTATIONS,'implementation must be explicitly independently selected')
 fixed={'schema':'numpy-teacher-numeric-contract-v2','formula':FORMULAS[c['implementation']],'precision':'FP32','vocabulary_logits':8192,'bos':2,'eos':False,'openblas_threads':2,'affinity_width':2,'runtime_contract_schema':'NUMPY_STRICT_RUNTIME_CLOSURE_V1'}
 for k,v in fixed.items():need(type(c[k])is type(v) and c[k]==v,'fixed exact numerical semantics')
 return digest(c)
def input_identity(prefix,tail):
 need(type(prefix)is list and type(tail)is list and 1<=len(prefix)<=64 and prefix[0]==2 and 1<=len(tail)<=63 and len(prefix)+len(tail)<=64,'full supported token geometry')
 need(all(type(t)is int and 0<=t<8192 for t in prefix) and all(type(t)is int and 3<=t<8192 for t in tail),'exact token integers')
 return digest({'schema':'full-prefix-tail-input-v1','prefix':prefix,'tail':tail})
def bind_inputs(inputs,c,*,hasher=lambda b:hashlib.sha256(b).hexdigest()):
 """Input-table primitive only; caller must pin/validate complete occurrence worklist.
 Test hash injection supports collision checks, not admitted production use.
 """
 ch=validate_contract(c);need(type(inputs)is list and inputs,'explicit nonempty supported input table')
 seen_input={};seen_keys={};out=[]
 for row in inputs:
  need(type(row)is dict and set(row)=={'input_id','prefix','tail'},'exact input schema')
  iid=input_identity(row['prefix'],row['tail']);need(row['input_id']==iid and iid not in seen_input,'exact unique input identity')
  raw=canonical({'schema':'numpy-teacher-numeric-key-v2','numeric_contract_sha256':ch,'input_id':iid,'prefix':row['prefix'],'tail':row['tail']})
  key=hasher(raw);need(hash64(key),'canonical numeric digest')
  need(key not in seen_keys or seen_keys[key]==raw,'numeric hash collision with different canonical bytes')
  need(key not in seen_keys,'numeric mapping must be one-to-one');seen_keys[key]=raw;seen_input[iid]=True
  out.append({'input_id':iid,'cache_key':key})
 return {'schema':'numpy-teacher-binding-v2','contract_sha256':ch,'bindings':out,'scoring_order':sorted(seen_keys),'real_scoring_authorized':False,'occurrence_mapping_verified':False}
