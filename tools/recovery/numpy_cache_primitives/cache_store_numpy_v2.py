"""Private immutable FP32 cache IO. No model, worklist, retries or quarantine.

All identities are caller-admitted; this library validates their structure,
not their scientific provenance. Unknown attempts require an external gate.
Entry operations are relative to an opened private dirfd with O_NOFOLLOW.
Ancestor checks are preflight, not protection against malicious concurrent
ancestor replacement; caller owns the isolated root and serial workload lease.
"""
import hashlib,json,math,os,re,stat,struct,secrets
from pathlib import Path
FIELDS={'schema','contract_sha256','cache_key','input_id','origin','value','fp32_le_hex','payload_sha256'}
def need(x,msg):
 if not x:raise ValueError(msg)
def canonical(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode()
def hex64(x):return type(x)is str and re.fullmatch('[0-9a-f]{64}',x) is not None
def fp32_bits(value):
 need(type(value)is float and math.isfinite(value) and value<=0.0,'finite nonpositive NumPy logp float, bool/int forbidden')
 try:raw=struct.pack('<f',value)
 except (OverflowError,struct.error):raise ValueError('FP32 range') from None
 restored=struct.unpack('<f',raw)[0];need(restored==value and math.copysign(1.0,restored)==math.copysign(1.0,value),'no implicit FP64 rounding');return raw.hex()
def origin_ok(origin):
 need(type(origin)is dict and set(origin)=={'window','attempt'},'origin schema');need(type(origin['window'])is int and origin['window']>=0,'window integer');need(type(origin['attempt'])is str and re.fullmatch('[A-Za-z0-9_-]{1,80}',origin['attempt']) is not None,'attempt identity')
def payload(contract_sha256,cache_key,input_id,value,origin):
 need(all(hex64(x) for x in [contract_sha256,cache_key,input_id]),'exact SHA identities');origin_ok(origin)
 p={'schema':'numpy-private-score-v2','contract_sha256':contract_sha256,'cache_key':cache_key,'input_id':input_id,'origin':dict(origin),'value':value,'fp32_le_hex':fp32_bits(value)};p['payload_sha256']=hashlib.sha256(canonical(p)).hexdigest();return p
def validate(p,contract_sha256,cache_key,input_id):
 need(type(p)is dict and set(p)==FIELDS,'exact score schema');expected=payload(contract_sha256,cache_key,input_id,p['value'],p['origin']);need(canonical(p)==canonical(expected),'score identity/bits/checksum mismatch');return p
def loads(raw):
 def pairs(items):
  out={}
  for k,v in items:need(k not in out,'duplicate JSON field');out[k]=v
  return out
 return json.loads(raw,object_pairs_hook=pairs,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('nonfinite JSON')))
class CacheStore:
 def __init__(self,root):
  self.root=Path(root).absolute();self.fd=None
  for p in [*reversed(self.root.parents),self.root]:
   if p.exists() or p.is_symlink():need(not p.is_symlink(),'symlink directory forbidden')
  try:self.root.mkdir(mode=0o700)
  except FileExistsError:pass
  self.fd=os.open(self.root,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
  st=os.fstat(self.fd)
  if stat.S_IMODE(st.st_mode)!=0o700:self.close();raise ValueError('private root must already be0700')
 def close(self):
  if self.fd is not None:os.close(self.fd);self.fd=None
 def __enter__(self):return self
 def __exit__(self,*args):self.close()
 def _read(self,name):
  need(self.fd is not None,'store closed');fd=os.open(name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=self.fd)
  try:
   st=os.fstat(fd);need(stat.S_ISREG(st.st_mode) and stat.S_IMODE(st.st_mode)==0o600 and st.st_size<=16384,'private bounded regular score')
   with os.fdopen(fd,'rb',closefd=False) as f:raw=f.read(16385)
   need(len(raw)==st.st_size<=16384,'bounded exact read');return raw
  finally:os.close(fd)
 def read_cache_only(self,contract_sha256,cache_key,input_id):
  need(all(hex64(x) for x in [contract_sha256,cache_key,input_id]),'exact SHA identities')
  # Missing, malformed, corrupted and symlink entries raise. Never a cache miss.
  return validate(loads(self._read(cache_key+'.json')),contract_sha256,cache_key,input_id)
 def publish(self,contract_sha256,cache_key,input_id,value,origin,*,fault=lambda point:None):
  need(self.fd is not None,'store closed');p=payload(contract_sha256,cache_key,input_id,value,origin);raw=canonical(p)+b'\n';name=cache_key+'.json'
  try:existing=self.read_cache_only(contract_sha256,cache_key,input_id)
  except FileNotFoundError:existing=None
  if existing is not None:
   need(canonical(existing)==canonical(p),'immutable key conflict; use read-only access for reuse');return existing
  temp='.pending-'+secrets.token_hex(16);fd=os.open(temp,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=self.fd)
  try:
   with os.fdopen(fd,'wb',closefd=False) as f:f.write(raw);f.flush();os.fsync(fd)
   fault('after_temp_write');need(self._read(temp)==raw,'temporary byte readback');fault('before_link')
   # A concurrent publisher is not overwritten; caller must investigate/re-read.
   os.link(temp,name,src_dir_fd=self.fd,dst_dir_fd=self.fd,follow_symlinks=False);fault('after_link');os.fsync(self.fd)
   return self.read_cache_only(contract_sha256,cache_key,input_id)
  finally:
   os.close(fd)
   try:os.unlink(temp,dir_fd=self.fd)
   except FileNotFoundError:pass
   os.fsync(self.fd)
 def verify_window(self,expected):
  """Fresh caller process must read actual files, not accept hash-only control.

  expected entries: contract_sha256,cache_key,input_id,payload_sha256.
  No score values returned: only immutable file/payload hashes and counts.
  """
  need(type(expected)is list and len(expected)>0,'nonempty fixed window key set');seen=set();rows=[]
  for ref in expected:
   need(type(ref)is dict and set(ref)=={'contract_sha256','cache_key','input_id','payload_sha256'},'exact expected reference');need(hex64(ref['payload_sha256']),'expected payload hash');key=ref['cache_key'];need(key not in seen,'duplicate expected key');seen.add(key)
   p=self.read_cache_only(ref['contract_sha256'],key,ref['input_id']);need(p['payload_sha256']==ref['payload_sha256'],'expected score identity');raw=self._read(key+'.json');need(validate(loads(raw),ref['contract_sha256'],key,ref['input_id'])['payload_sha256']==ref['payload_sha256'],'stable second read')
   rows.append({'cache_key':key,'payload_sha256':p['payload_sha256'],'file_sha256':hashlib.sha256(raw).hexdigest()})
  return {'schema':'numpy-cache-files-readback-v2','actual_score_files_verified':len(rows),'records':rows,'model_calls':0,'occurrence_mapping_verified':False,'limitation':'Caller must separately validate full occurrence mapping and fresh-process execution; this helper does not claim either.'}
