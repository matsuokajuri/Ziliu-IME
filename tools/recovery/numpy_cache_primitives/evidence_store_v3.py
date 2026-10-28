"""Lossless canonical checkpoint factoring. No guard/scorer/remote interface.
Only original canonical JSON (optional single newline) accepted, not silently
normalized. Reconstructs original bytes and validates all bindings. Pending
writes remain evidence; no cleanup, source change or overwrite recovery here.
Use one bounded execution window per store: _clean scans all references on
each append/read (not a scalable single-root full-dataset implementation).
_clean checks inventory/references, not every prior reference semantic binding;
verify_all performs complete expected-sequence and semantic readback.
"""
import fcntl,hashlib,json,os,secrets,stat
from pathlib import Path
import storage_admission_v2 as disk

def need(v,m):
 if not v:raise ValueError(m)
def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def sha(b):return hashlib.sha256(b).hexdigest()
def hash64(s):return type(s)is str and len(s)==64 and all(x in '0123456789abcdef' for x in s) and s!='0'*64
def loads(data):
 def pairs(items):
  d={}
  for k,v in items:need(k not in d,'duplicate JSON key');d[k]=v
  return d
 return json.loads(data,object_pairs_hook=pairs,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('nonfinite JSON')))
MAP_FIELDS={'observed_maps_private','after_maps_private','observed_maps_sha256','after_maps_sha256'}
class EvidenceStore:
 def __init__(self,root,identity_sha256,source_sha256,caps,*,create=False):
  need(hash64(identity_sha256) and hash64(source_sha256),'exact independent experiment/source bindings');self.caps=dict(caps);self.capsha=disk.validate(caps);self.identity=identity_sha256;self.source=source_sha256;self.root=Path(root).absolute();self.fd=None;self.lock=None
  for p in [*reversed(self.root.parents),self.root]:
   if p.exists() or p.is_symlink():need(not p.is_symlink(),'symlink root ancestor')
  if create:self.root.mkdir(mode=0o700)
  self.fd=os.open(self.root,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
  try:
   need(stat.S_IMODE(os.fstat(self.fd).st_mode)==0o700,'private root')
   self.lock=os.open('LOCK',os.O_RDWR|os.O_NOFOLLOW|(os.O_CREAT|os.O_EXCL if create else 0),0o600,dir_fd=self.fd);s=os.fstat(self.lock);need(stat.S_ISREG(s.st_mode) and stat.S_IMODE(s.st_mode)==0o600,'private lock');fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
   spec=canonical(dict(schema='lossless-checkpoint-store-v2',identity_sha256=self.identity,source_sha256=self.source,caps=self.caps,caps_sha256=self.capsha))
   if create:self._publish('SPEC.json',spec,16384)
   need(self._raw('SPEC.json',16384)==spec,'same exact store contract')
  except BaseException:self.close();raise
 def close(self):
  if self.lock is not None:os.close(self.lock);self.lock=None
  if self.fd is not None:os.close(self.fd);self.fd=None
 def __enter__(self):return self
 def __exit__(self,*a):self.close()
 def _raw(self,name,limit):
  need(self.fd is not None and Path(name).name==name,'flat private file');fd=os.open(name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=self.fd)
  try:
   s=os.fstat(fd);need(stat.S_ISREG(s.st_mode) and stat.S_IMODE(s.st_mode)==0o600 and s.st_size<=limit,'bounded private regular evidence')
   with os.fdopen(fd,'rb',closefd=False) as f:data=f.read(limit+1)
   after=os.fstat(fd);need(len(data)==s.st_size and (s.st_ino,s.st_size,s.st_mtime_ns)==(after.st_ino,after.st_size,after.st_mtime_ns),'stable bounded bytes');return data
  finally:os.close(fd)
 def _publish(self,name,data,limit,fault=lambda stage:None):
  need(type(data)is bytes and len(data)<=limit,'hard evidence cap, never truncate')
  try:existing=self._raw(name,limit)
  except FileNotFoundError:existing=None
  if existing is not None:need(existing==data,'existing blob/reference corrupt or conflicting; never treat as miss');return
  temp='.pending-'+secrets.token_hex(16);fd=os.open(temp,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=self.fd)
  try:
   fault('before_write')
   with os.fdopen(fd,'wb',closefd=False) as f:f.write(data);f.flush();os.fsync(fd)
   fault('after_file_fsync');need(self._raw(temp,limit)==data,'temp exact readback');fault('before_link');os.link(temp,name,src_dir_fd=self.fd,dst_dir_fd=self.fd,follow_symlinks=False);fault('after_link');os.fsync(self.fd);fault('after_dir_fsync');need(self._raw(name,limit)==data,'published bytes');os.unlink(temp,dir_fd=self.fd);os.fsync(self.fd)
  finally:os.close(fd)
 def _clean(self):
  names=set(os.listdir(self.fd));need(not any(n.startswith('.pending-') for n in names),'pending evidence requires reconciliation, no append')
  refs={n for n in names if n.startswith('checkpoint-')};used={'LOCK','SPEC.json'}|refs
  for name in refs:
   r=loads(self._raw(name,self.caps['reference_max_bytes']));need(type(r)is dict and 'closure_sha256'in r and 'maps'in r,'readable checkpoint reference')
   used.add('closure-'+r['closure_sha256']+'.json');used.update('map-'+h+'.blob' for h in r['maps'].values())
  need(names==used,'orphan/extra/missing evidence requires explicit reconciliation')
 def _binding(self,sequence,phase):
  need(type(sequence)is int and sequence>=0 and type(phase)is str and phase and len(phase)<=200,'sequence and phase')
  return dict(identity_sha256=self.identity,source_sha256=self.source,caps_sha256=self.capsha,sequence=sequence,phase=phase)
 def put_checkpoint(self,raw,sequence,phase,*,fault=lambda stage:None):
  self._clean();binding=self._binding(sequence,phase);need(type(raw)is bytes,'original bytes required');obj=loads(raw)
  need(type(obj)is dict and MAP_FIELDS|{'phase'}<=set(obj) and obj['phase']==phase,'complete original checkpoint schema/phase')
  newline=raw.endswith(b'\n');need(raw==canonical(obj)+(b'\n' if newline else b''),'only exact canonical originals accepted')
  refs=[n for n in os.listdir(self.fd) if n.startswith('checkpoint-')];name=f'checkpoint-{sequence:06d}.json'
  if name in refs:need(self.read_checkpoint(sequence,phase)==raw,'immutable original checkpoint conflict');return
  need(set(refs)=={f'checkpoint-{i:06d}.json' for i in range(sequence)},'ordered contiguous checkpoint sequence')
  closure={k:v for k,v in obj.items() if k not in MAP_FIELDS|{'phase'}};closure_raw=canonical(closure);ch=sha(closure_raw)
  # A store is one fixed runtime closure. Any new closure must fail, not rebind.
  closure_names=[n for n in os.listdir(self.fd) if n.startswith('closure-')]
  need(not closure_names or closure_names==['closure-'+ch+'.json'],'runtime closure changed')
  maps={}
  for side in ('observed','after'):
   text=obj[side+'_maps_private'];need(type(text)is str,'raw map string');data=text.encode();h=sha(data);need(h==obj[side+'_maps_sha256'],'original map SHA mismatch');need(len(data)<=self.caps['map_blob_max_bytes'],'map hard cap');maps[side]=(h,data)
  need(len(closure_raw)<=self.caps['closure_max_bytes'],'closure hard cap')
  reference=dict(schema='lossless-checkpoint-reference-v2',binding=binding,closure_sha256=ch,maps={k:h for k,(h,data) in maps.items()},original_sha256=sha(raw),original_canonical_sha256=sha(canonical(obj)),newline=newline)
  refraw=canonical(reference);need(len(refraw)<=self.caps['reference_max_bytes'],'reference hard cap')
  self._publish('closure-'+ch+'.json',closure_raw,self.caps['closure_max_bytes'],fault=lambda stage:fault('closure:'+stage))
  for side,(h,data) in maps.items():self._publish('map-'+h+'.blob',data,self.caps['map_blob_max_bytes'],fault=lambda stage:fault('map:'+stage))
  fault('before_reference');self._publish(name,refraw,self.caps['reference_max_bytes'],fault=lambda stage:fault('reference:'+stage));need(self.read_checkpoint(sequence,phase)==raw,'lossless original-byte reconstruction')
 def read_checkpoint(self,sequence,phase):
  self._clean();binding=self._binding(sequence,phase);r=loads(self._raw(f'checkpoint-{sequence:06d}.json',self.caps['reference_max_bytes']))
  need(set(r)=={'schema','binding','closure_sha256','maps','original_sha256','original_canonical_sha256','newline'} and r['schema']=='lossless-checkpoint-reference-v2' and canonical(r['binding'])==canonical(binding) and type(r['newline'])is bool,'exact checkpoint binding')
  need(hash64(r['closure_sha256']) and set(r['maps'])=={'observed','after'} and all(hash64(h) for h in r['maps'].values()),'exact content identities')
  closure=self._raw('closure-'+r['closure_sha256']+'.json',self.caps['closure_max_bytes']);need(sha(closure)==r['closure_sha256'],'closure corruption');obj=loads(closure);need(type(obj)is dict and not set(obj)&(MAP_FIELDS|{'phase'}),'exact closure partition');obj['phase']=phase
  for side,h in r['maps'].items():
   data=self._raw('map-'+h+'.blob',self.caps['map_blob_max_bytes']);need(sha(data)==h,'map corruption, never rebuild');obj[side+'_maps_private']=data.decode();obj[side+'_maps_sha256']=h
  rebuilt=canonical(obj);need(sha(rebuilt)==r['original_canonical_sha256'],'full reconstructed object hash');rebuilt+=b'\n' if r['newline'] else b'';need(sha(rebuilt)==r['original_sha256'],'exact original byte hash');return rebuilt
 def verify_all(self,expected):
  need(type(expected)is list and expected,'explicit complete checkpoint references');self._clean();rows=[];used={'LOCK','SPEC.json'}
  for i,row in enumerate(expected):
   need(type(row)is dict and set(row)=={'sequence','phase','original_sha256'} and type(row['sequence'])is int and row['sequence']==i,'complete expected order')
   raw=self.read_checkpoint(i,row['phase']);need(sha(raw)==row['original_sha256'],'external original-byte pin');name=f'checkpoint-{i:06d}.json';r=loads(self._raw(name,self.caps['reference_max_bytes']));used|={name,'closure-'+r['closure_sha256']+'.json'}|{'map-'+h+'.blob' for h in r['maps'].values()};rows.append(dict(row))
  need(set(os.listdir(self.fd))==used,'extra/orphan/missing evidence requires reconciliation')
  return dict(schema='lossless-evidence-actual-readback-v2',identity_sha256=self.identity,source_sha256=self.source,caps_sha256=self.capsha,checkpoints=rows,file_sha256={n:sha(self._raw(n, max(self.caps['closure_max_bytes'],self.caps['map_blob_max_bytes'],self.caps['reference_max_bytes'],16384))) for n in sorted(used-{'LOCK'})},model_calls=0,guard_validation_performed=False,fresh_process_verified_here=False)
