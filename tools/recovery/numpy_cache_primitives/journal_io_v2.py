"""Private immutable event IO. No model/process launcher/retry/remote interface.
Caught or killed partial writes remain evidence. Any pending/extra/gap state
fails closed; classification never infers process exit from missing files.
"""
import fcntl,hashlib,json,os,secrets,stat
from pathlib import Path
import cache_store_numpy_v2 as cache
import recovery_protocol_v2 as protocol
class Journal:
 def __init__(self,root,spec,*,create=False):
  self.root=Path(root).absolute();self.fd=None;self.lockfd=None;self.spec=spec;self.sh=protocol.spec_hash(spec)
  for p in [*reversed(self.root.parents),self.root]:
   if p.exists() or p.is_symlink():cache.need(not p.is_symlink(),'symlink journal ancestor')
  if create:self.root.mkdir(mode=0o700)
  self.fd=os.open(self.root,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
  try:
   cache.need(stat.S_IMODE(os.fstat(self.fd).st_mode)==0o700,'private0700 journal')
   flags=os.O_RDWR|os.O_NOFOLLOW|(os.O_CREAT|os.O_EXCL if create else 0)
   self.lockfd=os.open('LOCK',flags,0o600,dir_fd=self.fd)
   st=os.fstat(self.lockfd);cache.need(stat.S_ISREG(st.st_mode) and stat.S_IMODE(st.st_mode)==0o600,'private regular lock')
   fcntl.flock(self.lockfd,fcntl.LOCK_EX|fcntl.LOCK_NB)
   if create:self._publish('SPEC.json',{'spec':spec,'spec_sha256':self.sh})
   cache.need(self._read('SPEC.json')=={'spec':spec,'spec_sha256':self.sh},'exact existing journal spec')
  except BaseException:self.close();raise
 def close(self):
  if self.lockfd is not None:os.close(self.lockfd);self.lockfd=None
  if self.fd is not None:os.close(self.fd);self.fd=None
 def __enter__(self):return self
 def __exit__(self,*args):self.close()
 def _raw(self,name):
  cache.need(Path(name).name==name and self.fd is not None,'flat name and open journal')
  fd=os.open(name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=self.fd)
  try:
   before=os.fstat(fd);cache.need(stat.S_ISREG(before.st_mode) and stat.S_IMODE(before.st_mode)==0o600 and before.st_size<=2**20,'private bounded regular receipt')
   with os.fdopen(fd,'rb',closefd=False) as f:data=f.read(2**20+1)
   after=os.fstat(fd);cache.need(len(data)==before.st_size and (before.st_ino,before.st_size,before.st_mtime_ns)==(after.st_ino,after.st_size,after.st_mtime_ns),'stable bounded receipt read');return data
  finally:os.close(fd)
 def _read(self,name):return cache.loads(self._raw(name))
 def _publish(self,name,value,*,fault=lambda stage:None):
  cache.need(Path(name).name==name,'flat immutable name');data=cache.canonical(value)+b'\n';temp='.pending-'+secrets.token_hex(16)
  fd=os.open(temp,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=self.fd)
  try:
   fault('before_write')
   with os.fdopen(fd,'wb',closefd=False) as stream:stream.write(data);stream.flush();fault('before_file_fsync');os.fsync(fd)
   fault('after_file_fsync');cache.need(self._raw(temp)==data,'temporary exact readback');fault('before_link')
   os.link(temp,name,src_dir_fd=self.fd,dst_dir_fd=self.fd,follow_symlinks=False);fault('after_link');os.fsync(self.fd);fault('after_dir_fsync')
   cache.need(self._raw(name)==data,'published exact readback');os.unlink(temp,dir_fd=self.fd);os.fsync(self.fd)
  finally:os.close(fd)
 def verified_events(self):
  names=set(os.listdir(self.fd));cache.need({'LOCK','SPEC.json'}<=names,'complete journal roots')
  extras=names-{'LOCK','SPEC.json'};cache.need(all((x.startswith('event-') and len(x)==17 and x[6:12].isdigit() or x.startswith('commit-') and len(x)==18 and x[7:13].isdigit()) and x.endswith('.json') for x in extras),'pending/extra receipts require reconciliation')
  n=sum(x.startswith('event-') for x in extras);ordered=[f'event-{i:06d}.json' for i in range(n)];commits=[f'commit-{i:06d}.json' for i in range(n)];cache.need(set(ordered+commits)==extras,'gap/truncated or uncommitted journal sequence')
  events=[self._read(name) for name in ordered];protocol.inspect(self.spec,events)
  for i,name in enumerate(commits):cache.need(self._read(name)=={'sequence':i,'spec_sha256':self.sh,'event_file_sha256':hashlib.sha256(self._raw(ordered[i])).hexdigest()},'independent immutable commit watermark')
  file_hashes={name:hashlib.sha256(self._raw(name)).hexdigest() for name in ['SPEC.json']+ordered+commits}
  cache.need(names==set(os.listdir(self.fd)),'journal inventory changed during readback')
  return events,{'schema':'numpy-journal-actual-readback-v2','spec_sha256':self.sh,'file_sha256':file_hashes,'events_sha256':protocol.digest(events),'model_calls':0,'fresh_process_proven_here':False}
 def append(self,kind,data,*,fault=lambda stage:None):
  events,_=self.verified_events();event=protocol.event(self.spec,events,kind,data)
  protocol.inspect(self.spec,events+[event]);name=f'event-{len(events):06d}.json';self._publish(name,event,fault=fault)
  self._publish(f'commit-{len(events):06d}.json',{'sequence':len(events),'spec_sha256':self.sh,'event_file_sha256':hashlib.sha256(self._raw(name)).hexdigest()})
  actual,receipt=self.verified_events();cache.need(actual==events+[event],'complete original append readback');return receipt
 def classify(self,*,process_state='UNKNOWN',source_integrity_verified=False):
  events,receipt=self.verified_events()
  # EXITED alone cannot prove no lost trailing issued event. Only an external
  # confirmed NOT_LAUNCHED observation can admit a no-start classification.
  observation='UNKNOWN' if process_state=='EXITED' else process_state
  state=protocol.inspect(self.spec,events,process_state=observation,source_integrity_verified=source_integrity_verified)
  return {'classification':state,'actual_readback':receipt,'verified_disk_evidence_sha256':protocol.digest(receipt)}
