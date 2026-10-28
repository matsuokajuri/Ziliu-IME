"""Pure disk/inode admission arithmetic; no allocation, deletion or scoring."""
import hashlib,json,os
FIELDS={'schema','map_blob_max_bytes','closure_max_bytes','reference_max_bytes','record_max_bytes','window_control_max_bytes','safety_reserve_bytes','inode_reserve','emergency_bytes'}
DEFAULT={'schema':'numpy-evidence-storage-caps-v2','map_blob_max_bytes':32768,'closure_max_bytes':131072,'reference_max_bytes':4096,'record_max_bytes':4096,'window_control_max_bytes':2097152,'safety_reserve_bytes':8*2**30,'inode_reserve':200000,'emergency_bytes':2**30}
def need(v,m):
 if not v:raise ValueError(m)
def validate(c):
 need(type(c)is dict and set(c)==FIELDS and c['schema']=='numpy-evidence-storage-caps-v2','exact storage caps')
 for k in FIELDS-{'schema'}:need(type(c[k])is int and c[k]>0,'positive exact byte/inode cap')
 need(c['emergency_bytes']<=c['safety_reserve_bytes'],'emergency is reserved, not normal budget')
 return hashlib.sha256(json.dumps(c,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def rounded(n,unit):
 need(type(n)is int and n>=0 and type(unit)is int and unit>0,'nonnegative bytes/exact allocation unit');return ((n+unit-1)//unit)*unit
def budget(c,score_calls,windows,allocation_unit):
 ch=validate(c)
 for n in (score_calls,windows):need(type(n)is int and n>=0,'exact nonnegative counts')
 per=4*rounded(c['map_blob_max_bytes'],allocation_unit)+2*rounded(c['reference_max_bytes'],allocation_unit)+7*rounded(c['record_max_bytes'],allocation_unit)
 total=score_calls*per+windows*(rounded(c['closure_max_bytes'],allocation_unit)+rounded(c['window_control_max_bytes'],allocation_unit))
 return dict(caps_sha256=ch,allocated_bytes=total,inodes=13*score_calls+40*windows,allocation_unit=allocation_unit,scope='worst-case proposed records only; caller must include pilot duplicates and additional artifacts')
def admit(c,score_calls,windows,observation,*,extra_bytes=0,extra_inodes=0):
 need(type(observation)is dict and set(observation)=={'available_bytes','available_inodes','allocation_unit'},'exact fresh statvfs observation')
 for v in observation.values():need(type(v)is int and v>=0,'valid storage observation')
 for n in (extra_bytes,extra_inodes):need(type(n)is int and n>=0,'explicit extra storage')
 b=budget(c,score_calls,windows,observation['allocation_unit']);required=b['allocated_bytes']+c['safety_reserve_bytes']+extra_bytes;inode_required=b['inodes']+c['inode_reserve']+extra_inodes
 return dict(status='STORAGE_ADMITTED' if observation['available_bytes']>=required and observation['available_inodes']>=inode_required else 'STOP_LOW_STORAGE_BEFORE_NEW_ISSUED',required_bytes=required,required_inodes=inode_required,observation=dict(observation),budget=b,new_score_allowed=observation['available_bytes']>=required and observation['available_inodes']>=inode_required,space_actually_reserved=False,quota_verified=False)
def observe(path):
 s=os.statvfs(path);return dict(available_bytes=s.f_bavail*s.f_frsize,available_inodes=s.f_favail,allocation_unit=s.f_frsize)
