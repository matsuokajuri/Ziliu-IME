"""One fresh public-base head update, original loss/math; no implicit actual admission."""
import base64,hashlib,json,os,random,time
from pathlib import Path

def need(v,m):
 if not v:raise ValueError(m)
def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def digest(v):return hashlib.sha256(canonical(v)).hexdigest()
def file_sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  while b:=f.read(65536):h.update(b)
 return h.hexdigest()
def put(path,value):
 raw=canonical(value)+b'\n';need(len(raw)<=1024*1024,'bounded metadata')
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
 with os.fdopen(fd,'wb') as f:f.write(raw);f.flush();os.fsync(f.fileno())
 fd=os.open(Path(path).parent,os.O_RDONLY|os.O_DIRECTORY)
 try:os.fsync(fd)
 finally:os.close(fd)
 need(Path(path).read_bytes()==raw,'exact durable readback')
def rng_snapshot(torch,np):
 n=np.random.get_state();need(n[0]=='MT19937','known numpy RNG')
 return {'python':random.getstate(),'numpy':{'algorithm':n[0],'keys':n[1].tolist(),'position':n[2],'has_gauss':n[3],'cached_gaussian':n[4]},'torch_cpu':base64.b64encode(torch.get_rng_state().numpy().tobytes()).decode()}
def restore_rng(v,torch,np):
 need(set(v)=={'python','numpy','torch_cpu'},'closed RNG')
 def tuples(x):return tuple(tuples(y) for y in x) if type(x)is list else x
 random.setstate(tuples(v['python']));n=v['numpy'];need(set(n)=={'algorithm','keys','position','has_gauss','cached_gaussian'} and n['algorithm']=='MT19937' and len(n['keys'])==624,'numpy RNG shape')
 np.random.set_state((n['algorithm'],np.asarray(n['keys'],dtype=np.uint32),n['position'],n['has_gauss'],n['cached_gaussian']))
 raw=base64.b64decode(v['torch_cpu'],validate=True);need(0<len(raw)<=65536,'torch RNG bound');torch.set_rng_state(torch.from_numpy(np.frombuffer(raw,dtype=np.uint8).copy()))
 need(digest(rng_snapshot(torch,np))==digest(v),'exact RNG restore')
def state_seal(model,opt):
 def arr(v):
  a=v.detach().cpu().numpy();return {'shape':list(a.shape),'dtype':str(a.dtype),'sha256':hashlib.sha256(a.tobytes()).hexdigest()}
 return {'parameters':{n:arr(p) for n,p in model.named_parameters()},'adam':{n:{k:arr(v) for k,v in opt.state.get(p,{}).items()} for n,p in model.named_parameters()},'embedding':arr(model.embedding)}
def save(root,step,history,identity,previous,model,opt,torch,np,checkpoint):
 path=root/f'step-{step:03d}.npz';rng=rng_snapshot(torch,np);before=state_seal(model,opt)
 sha=checkpoint.save_checkpoint(path,model,opt,step,history,identity,previous_sha256=previous)
 need(sha==file_sha(path),'checkpoint FILE')
 side={'schema':'PUBLIC_HEAD_RNG_BINDING_V1','checkpoint_file_sha256':sha,'identity_sha256':digest(identity),'step':step,'rng':rng,'state_seal':before}
 put(root/f'step-{step:03d}-rng.json',side)
 meta=checkpoint.load_checkpoint(path,model,opt,identity);restore_rng(json.loads(canonical(rng)),torch,np)
 need(meta['step']==step and state_seal(model,opt)==before,'full head Adam restore')
 return sha

def validate_scene(scene,identity):
 need(type(scene)is dict and set(scene)=={'scene_id','target','given_prefix_tokens','modes'},'closed selected scene')
 need(scene['scene_id']==identity['schedule'][0][0] and type(scene['target'])is str and scene['target'],'exact selected TRAIN scene')
 c=scene['given_prefix_tokens'];need(type(c)is list and len(c)<=64 and all(type(i)is int and 1<=i<8192 and i!=2 for i in c),'frozen original context')
 modes=scene['modes'];need(type(modes)is list and 1<=len(modes)<=2,'all eligible modes')
 need(len({m['mode'] for m in modes})==len(modes),'unique modes')
 for m in modes:
  need(type(m)is dict and set(m)=={'mode','graph'} and m['mode'] in ('pureinitial','mixed'),'mode graph')
 return modes

def run(root,identity,scene,model,opt,torch,np,packed,objective,checkpoint,contract,native_factory,genesis_binding):
 """Caller must bind all supplied objects and exact materials under reviewed runtime.
 Fresh root only. Failure consumes root and permanently records UNKNOWN; no resume.
 """
 root=Path(root);root.mkdir(mode=0o700);stage='GENESIS'
 try:
  put(root/'ISSUED.json',{'schema':'PUBLIC_HEAD_ONE_UPDATE_ISSUED_V1','identity_sha256':digest(identity),'automatic_retry':False})
  contract.validate(identity);modes=validate_scene(scene,identity)
  need(type(genesis_binding)is dict and set(genesis_binding)=={'checkpoint','rng','identity_sha256','review_file_sha256'},'exact admitted genesis binding')
  need(genesis_binding['identity_sha256']==digest(identity) and type(genesis_binding['review_file_sha256'])is str and len(genesis_binding['review_file_sha256'])==64,'genesis identity/review')
  cpref=genesis_binding['checkpoint'];rr=genesis_binding['rng']
  for ref in (cpref,rr):
   need(type(ref)is dict and set(ref)=={'path','bytes','sha256'} and type(ref['bytes'])is int and 0<ref['bytes']<=2*2**20 and Path(ref['path']).stat().st_size==ref['bytes'],'bounded exact genesis artifact')
  meta=restore(cpref['path'],rr['path'],cpref['sha256'],rr['sha256'],identity,model,opt,torch,np,checkpoint)
  need(meta['step']==0 and all(not opt.state.get(p,{}) for p in model.parameters()),'qualified Adam0')
  need(all(torch.isfinite(p).all() for p in model.parameters()),'finite genesis')
  need(torch.count_nonzero(model.output.weight).item()==0,'zero output initialization')
  embedding=state_seal(model,opt)['embedding'];rng=rng_snapshot(torch,np)
  previous=cpref['sha256']
  # Preserve exact qualified genesis pair; never silently reinitialize step0.
  for ref,name in ((cpref,'step-000.npz'),(rr,'step-000-rng.json')):
   raw=Path(ref['path']).read_bytes();need(hashlib.sha256(raw).hexdigest()==ref['sha256'],'same genesis bytes')
   with (root/name).open('xb') as f:f.write(raw);f.flush();os.fsync(f.fileno())
  checkpoint.fsync_dir(root)
  stage='UPDATE';begin=time.monotonic();opt.zero_grad(set_to_none=True);losses=[];counts={};conditions=0
  for item in modes:
   graph=item['graph']
   with native_factory(graph) as native:
    for context in ([],scene['given_prefix_tokens']):
     prepared=packed.prepare_graph(graph,item['mode'],model.index,context)
     residuals=model(prepared,context)
     search=native.search(residuals.detach().double().tolist())
     loss,observed=objective.condition_loss(graph,scene['target'],residuals,search,prepared['valid'])
     need(loss is not None and torch.isfinite(loss),'eligible complete gold path')
     losses.append(loss);conditions+=1
     for key,value in observed.items():counts[key]=counts.get(key,0)+value
  need(len(losses) in (2,4),'complete original conditions')
  # Exactly the old scene mean, then one-scene batch mean; not pooled edge loss.
  scene_losses=[torch.stack(losses).mean()];loss=torch.stack(scene_losses).mean();need(torch.isfinite(loss),'finite loss')
  loss.backward();need(all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters()),'finite complete gradients')
  norm=torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True);need(torch.isfinite(norm) and norm>0,'first nonzero gradient')
  opt.step();need(all(torch.isfinite(p).all() for p in model.parameters()),'finite updated head')
  need(not model.embedding.requires_grad and model.embedding.grad is None and state_seal(model,opt)['embedding']==embedding,'frozen public E')
  need(digest(rng_snapshot(torch,np))==digest(rng),'training RNG unchanged')
  info={'step':1,'scene_ids':[scene['scene_id']],'scenes':1,'conditions':conditions,'graphs':len(modes),'loss':loss.item(),'gradient_norm':norm.item(),'seconds':time.monotonic()-begin,'boundaries':counts}
  stage='CHECKPOINT';current=save(root,1,[info],identity,previous,model,opt,torch,np,checkpoint)
  result={'status':'PUBLIC_HEAD_ONE_UPDATE_COMPLETE_BACKUP_REQUIRED','identity_sha256':digest(identity),'checkpoint0_file_sha256':previous,'checkpoint1_file_sha256':current,'state_seal':state_seal(model,opt),'rng_sha256':digest(rng_snapshot(torch,np)),'step':1,'scene_cursor':1,'info':info,'remote_backup_verified':False,'next_update_authorized':False}
  put(root/'COMPLETE.json',result);return result
 except BaseException as e:
  put(root/'UNKNOWN.json',{'stage':stage,'type':type(e).__name__,'automatic_retry':False})
  raise RuntimeError('PUBLIC_HEAD_OPERATION_UNKNOWN') from None

def restore(path,side_path,checkpoint_sha,side_sha,identity,model,opt,torch,np,checkpoint):
 """Exact independently bound pair, not a caller's unverified embedded digest."""
 need(file_sha(path)==checkpoint_sha and file_sha(side_path)==side_sha,'external exact pair')
 raw=Path(side_path).read_bytes();need(len(raw)<=1024*1024,'bounded RNG sidecar');side=json.loads(raw)
 need(set(side)=={'schema','checkpoint_file_sha256','identity_sha256','step','rng','state_seal'} and side['schema']=='PUBLIC_HEAD_RNG_BINDING_V1','closed RNG binding')
 need(side['checkpoint_file_sha256']==checkpoint_sha and side['identity_sha256']==digest(identity) and type(side['step'])is int and side['step'] in (0,1),'pair identity/step')
 meta=checkpoint.load_checkpoint(path,model,opt,identity)
 need(meta['step']==side['step'] and state_seal(model,opt)==side['state_seal'],'exact full p/Adam/E restore')
 restore_rng(side['rng'],torch,np)
 need(file_sha(path)==checkpoint_sha and file_sha(side_path)==side_sha,'read-only pair unchanged')
 return meta

def genesis(root,identity,model,opt,torch,np,checkpoint,contract):
 """Separate actual phase: no graph/target/native/update. Must be reviewed before update."""
 root=Path(root);root.mkdir(mode=0o700)
 try:
  put(root/'ISSUED.json',{'phase':'GENESIS_ONLY','identity_sha256':digest(identity)})
  contract.validate(identity);need(len(opt.state)==0,'fresh empty Adam')
  need(all(torch.isfinite(p).all() for p in model.parameters()) and torch.count_nonzero(model.output.weight).item()==0,'finite zero-output head')
  cp=save(root,0,[],identity,None,model,opt,torch,np,checkpoint)
  side=root/'step-000-rng.json';side_sha=file_sha(side)
  restore(root/'step-000.npz',side,cp,side_sha,identity,model,opt,torch,np,checkpoint)
  result={'status':'PUBLIC_HEAD_GENESIS_EXACT_RESTORE_VERIFIED_NOT_UPDATE_ADMISSION','identity_sha256':digest(identity),'checkpoint_file_sha256':cp,'rng_sidecar_file_sha256':side_sha,'step':0,'scene_cursor':0,'updates':0}
  put(root/'COMPLETE.json',result);return result
 except BaseException as e:
  put(root/'UNKNOWN.json',{'phase':'GENESIS_ONLY','type':type(e).__name__});raise RuntimeError('PUBLIC_HEAD_GENESIS_UNKNOWN') from None
