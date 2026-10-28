"""Fail-closed local supervisor. No shell, automatic retry or recovery killing."""
import hashlib,json,os,selectors,signal,subprocess,time
from pathlib import Path

def canonical(x):return json.dumps(x,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def digest(x):return hashlib.sha256(canonical(x)).hexdigest()
def need(ok,msg):
 if not ok:raise RuntimeError(msg)
def durable(path,data):
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
 with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
 fd=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY);os.fsync(fd);os.close(fd)
 need(path.read_bytes()==data,'durable readback')
def receipt(root,name,value):durable(root/name,canonical(value)+b'\n')
def start_identity(pid):
 # comm may contain parentheses/spaces; tail starts at stat field 3.
 text=Path(f'/proc/{pid}/stat').read_text();tail=text[text.rfind(')')+2:].split()
 return {'pid':pid,'start_ticks':int(tail[19]),'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip()}
def owned_group_members(leader,identity):
 need(start_identity(leader)==identity and os.getpgid(leader)==os.getsid(leader)==leader,'unreaped exact session leader')
 members=[]
 for path in Path('/proc').iterdir():
  if not path.name.isdecimal():continue
  try:
   raw=(path/'stat').read_text();tail=raw[raw.rfind(')')+2:].split()
  except FileNotFoundError:continue # exited before observation
  pid=int(path.name);pgid=int(tail[2]);sid=int(tail[3])
  if pgid==leader:
   need(sid==leader,'owned group must remain original session')
   if pid!=leader:members.append({'pid':pid,'start_ticks':int(tail[19]),'state':tail[0],'pgid':pgid,'session':sid})
 return sorted(members,key=lambda x:x['pid'])
def validate(m,review):
 need(type(m)is dict and set(m)=={'argv','env','cwd','source_sha256','deadline_seconds','kill_grace_seconds','stream_cap_bytes'},'exact manifest schema')
 need(review=={'approved':True,'manifest_sha256':digest(m)} and type(review['approved'])is bool,'exact independent review')
 a=m['argv'];need(type(a)is list and len(a)>0 and all(type(x)is str and '\0'not in x for x in a),'argv strings')
 exe=Path(a[0]);need(exe.is_absolute() and exe.is_file() and not exe.is_symlink(),'absolute regular executable')
 need(exe.name not in {'sh','bash','dash','zsh','fish','cmd.exe','powershell','pwsh'},'no shell launcher')
 need(type(m['env'])is dict and all(type(k)is str and type(v)is str and k and '='not in k and '\0'not in k+v for k,v in m['env'].items()),'exact environment')
 cwd=Path(m['cwd']);need(cwd.is_absolute() and cwd.is_dir() and not cwd.is_symlink(),'absolute plain cwd')
 need(type(m['source_sha256'])is dict and str(exe)in m['source_sha256'] and str(Path(__file__).resolve())in m['source_sha256'],'executable and supervisor pinned')
 for name,h in m['source_sha256'].items():
  p=Path(name);need(p.is_absolute() and p.is_file() and not p.is_symlink() and hashlib.sha256(p.read_bytes()).hexdigest()==h,'same source bytes')
 for n,maximum in [('deadline_seconds',120),('kill_grace_seconds',10)]:need(type(m[n])in (int,float) and 0<m[n]<=maximum,'bounded timeout')
 need(type(m['stream_cap_bytes'])is int and 0<m['stream_cap_bytes']<=4*2**20,'bounded stream cap')
def recover(root):
 root=Path(root)
 if not root.exists():return 'ABSENT'
 # Never authorize retry or signal an old PID, even after a terminal receipt.
 return 'TERMINAL_REVIEW_REQUIRED' if (root/'TERMINAL.json').is_file() else 'UNKNOWN_BLOCKED'
def run(manifest,review,root):
 validate(manifest,review);root=Path(root)
 need(root.is_absolute() and not root.exists() and not root.is_symlink(),'new attempt only; existing attempt blocks retry')
 root.mkdir(mode=0o700);receipt(root,'MANIFEST.json',manifest);receipt(root,'REVIEW.json',review)
 receipt(root,'INTENT.json',{'manifest_sha256':digest(manifest),'automatic_retry':False,'monotonic_ns':time.monotonic_ns()})
 child=None;identity=None;selector=selectors.DefaultSelector();streams={'stdout':bytearray(),'stderr':bytearray()};observed={'stdout':0,'stderr':0};reason=None;term_at=None;kill_sent=False;began=time.monotonic();returncode=None;error=None;logs={};eof={name:False for name in streams};group_observations=[];group_clear=False
 def owned_signal(sig):
  need(child is not None and identity is not None,'known owned child')
  # Leader remains unreaped until the end, preventing PID/group reuse.
  need(start_identity(child.pid)==identity and os.getpgid(child.pid)==os.getsid(child.pid)==child.pid,'exact owned live/zombie session leader')
  os.killpg(child.pid,sig)
 def group_snapshot(label):
  members=owned_group_members(child.pid,identity);group_observations.append({'phase':label,'monotonic_ns':time.monotonic_ns(),'members':members});return members
 def exited():return os.waitid(os.P_PID,child.pid,os.WEXITED|os.WNOHANG|os.WNOWAIT) is not None
 try:
  for name in streams:logs[name]=os.open(root/(name+'.bin'),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
  child=subprocess.Popen(manifest['argv'],cwd=manifest['cwd'],env=manifest['env'],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True,close_fds=True)
  identity=start_identity(child.pid);receipt(root,'CHILD.json',dict(identity,pgid=child.pid,manifest_sha256=digest(manifest)))
  for name,pipe in [('stdout',child.stdout),('stderr',child.stderr)]:os.set_blocking(pipe.fileno(),False);selector.register(pipe,selectors.EVENT_READ,name)
  while True:
   now=time.monotonic()
   if reason is None and now-began>=manifest['deadline_seconds']:reason='DEADLINE_EXCEEDED'
   if reason is not None and term_at is None:owned_signal(signal.SIGTERM);term_at=now
   if term_at is not None and not kill_sent and now-term_at>=manifest['kill_grace_seconds']:owned_signal(signal.SIGKILL);kill_sent=True
   for key,_ in selector.select(.02):
    data=os.read(key.fileobj.fileno(),65536)
    if not data:eof[key.data]=True;selector.unregister(key.fileobj);key.fileobj.close();continue
    name=key.data;observed[name]+=len(data);cap=manifest['stream_cap_bytes'];chunk=data[:max(0,cap-len(streams[name]))];streams[name].extend(chunk)
    if chunk:
     written=0
     while written<len(chunk):written+=os.write(logs[name],chunk[written:])
     os.fsync(logs[name])
    if observed[name]>cap and reason is None:reason='OUTPUT_OVERFLOW'
   if exited() and not selector.get_map():
    members=group_snapshot('leader-exited-eof')
    if not members:group_clear=True;break
    if reason is None:reason='OWNED_GROUP_RESIDUAL'
    if all(m['state']=='Z' for m in members):reason='UNKNOWN_ZOMBIE_GROUP_RESIDUAL';break
   if kill_sent and now-term_at>=manifest['kill_grace_seconds']+1:
    members=group_snapshot('post-kill-bound');group_clear=not members
    if members:reason='UNKNOWN_GROUP_RESIDUAL'
    break
  if exited():returncode=child.wait()
  else:reason='UNKNOWN_CHILD_NOT_REAPED'
 except BaseException as e:
  error=e;reason=reason or 'SUPERVISOR_FAILURE'
  if child is not None:
   try:
    if identity is None:identity=start_identity(child.pid)
    owned_signal(signal.SIGTERM)
    until=time.monotonic()+manifest['kill_grace_seconds']
    while time.monotonic()<until:
     if exited() and not group_snapshot('exception-term-check'):break
     time.sleep(.01)
    owned_signal(signal.SIGKILL)
    until=time.monotonic()+1
    while time.monotonic()<until:
     if exited():
      members=group_snapshot('exception-kill-check')
      if not members or all(m['state']=='Z' for m in members):break
     time.sleep(.01)
    members=group_snapshot('exception-final');group_clear=not members
    if exited():returncode=child.wait()
   except BaseException:pass # Ambiguous cleanup remains explicit; no unrelated PID kill.
 finally:
  selector.close()
  for fd in logs.values():
   try:os.fsync(fd)
   except BaseException as e:
    if error is None:error=e;reason='SUPERVISOR_FAILURE'
   try:os.close(fd)
   except BaseException as e:
    if error is None:error=e;reason='SUPERVISOR_FAILURE'
  if child is not None:
   for pipe in (child.stdout,child.stderr):
    if pipe is not None:pipe.close()
 terminal={'manifest_sha256':digest(manifest),'identity':identity,'returncode':returncode,'status':reason or ('EXIT_ZERO' if returncode==0 else 'EXIT_NONZERO'),'elapsed_seconds':time.monotonic()-began,'streams':{},'capture_eof_complete':all(eof.values()),'stream_eof':eof,'owned_group_clear_before_reap':group_clear,'owned_group_observations':group_observations,'automatic_retry':False,'cleanup_confirmed_reaped':child is not None and returncode is not None}
 try:
  for name,data in streams.items():
   need((root/(name+'.bin')).read_bytes()==bytes(data),'exact captured output readback');terminal['streams'][name]={'observed_bytes':observed[name],'observed_bytes_are_lower_bound':not eof[name],'retained_bytes':len(data),'truncated':observed[name]>len(data),'sha256':hashlib.sha256(data).hexdigest()}
  receipt(root,'TERMINAL.json',terminal)
 except BaseException:
  if error is None:raise
 if error is not None:raise error
 return terminal
