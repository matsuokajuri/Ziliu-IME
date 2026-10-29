"""Exactly four new ranking updates, then mandatory remote recovery barrier."""
from pathlib import Path

def run(root,identity,batches,start_update,*,prior_pair,expected_input_previous_sha256,backup_anchor,restore_input,
        model,opt,torch,np,packed,admission,adapter,ranking,objective,native_factory,
        contract,checkpoint,parent_validate,op):
 root=Path(root);root.mkdir(mode=0o700);stage='ADMISSION'
 try:
  indices=contract.window(identity,start_update,parent_validate)
  contract.need(type(batches)is list and len(batches)==4,'exact four-update window')
  contract.need([[s['scene_id']for s in batch]for batch in batches]==[identity['schedule'][i]for i in indices],'exact remaining schedule window')
  contract.need(type(prior_pair)is dict and set(prior_pair)=={'checkpoint','rng'},'exact previous pair')
  contract.need(type(backup_anchor)is dict and set(backup_anchor)=={'status','input_checkpoint_sha256','input_rng_sha256','restore_review_sha256','head_review_sha256'},'closed backup admission')
  contract.need(backup_anchor['status']=='PASS_PRIOR_PAIR_REMOTE_RESTORE_AND_HEAD_VERIFIED' and backup_anchor['input_checkpoint_sha256']==prior_pair['checkpoint']['sha256'] and backup_anchor['input_rng_sha256']==prior_pair['rng']['sha256'] and all(contract.h64(backup_anchor[k])for k in('restore_review_sha256','head_review_sha256')),'prior complete remote recovery barrier')
  for ref in prior_pair.values():contract.need(type(ref)is dict and set(ref)=={'path','bytes','sha256'} and type(ref['bytes'])is int and 0<ref['bytes']<=2*2**20 and Path(ref['path']).stat().st_size==ref['bytes'] and op.file_sha(ref['path'])==ref['sha256'],'exact input bytes')
  contract.need(ranking.MARGIN==1.0 and ranking.RANKING_WEIGHT==1.0,'fixed objective constants')
  op.put(root/'ISSUED.json',{'schema':'RANK_REMAINING93_WINDOW_ISSUED_V1','identity_sha256':op.digest(identity),'start_update':start_update,'end_update':start_update+4,'automatic_retry':False})
  stage='INPUT_RESTORE';contract.need(contract.h64(expected_input_previous_sha256),'explicit input predecessor');meta=restore_input(prior_pair,expected_input_previous_sha256)
  contract.need(type(meta)is dict and meta['previous_checkpoint_sha256']==expected_input_previous_sha256,'exact restored input predecessor')
  pairs=[]
  if start_update==0:
   contract.need(type(meta['logical_update'])is int and meta['logical_update']==1 and type(meta['adam_step'])is int and meta['adam_step']==26 and type(meta['scene_cursor'])is int and meta['scene_cursor']==4 and type(meta['history'])is list and len(meta['history'])==1 and op.digest(meta['identity'])==identity['parent_identity_sha256'],'exact fork1 only')
   contract.need(prior_pair['checkpoint']['sha256']==identity['parent_checkpoint_sha256'] and prior_pair['rng']['sha256']==identity['parent_rng_file_sha256'],'exact parent fork1 pair')
   contract.need(op.digest(op.state_seal(model,opt))==identity['parent_state_sha256'] and op.digest(op.rng_snapshot(torch,np))==identity['parent_rng_state_sha256'],'migration preserves full fork1 state/RNG')
   history=[];previous=prior_pair['checkpoint']['sha256'];stage='MIGRATION'
   migration=checkpoint.save_pair(root,identity,0,history,previous,model,opt,torch,np,contract,parent_validate,op)
   checkpoint.restore_pair(migration,identity,model,opt,torch,np,contract,parent_validate,op,expected_previous_sha256=previous)
   pairs.append({'logical_update':0,'pair':migration,'previous_checkpoint_sha256':previous,'state_seal':op.state_seal(model,opt),'rng_state_sha256':op.digest(op.rng_snapshot(torch,np))});previous=migration['checkpoint']['sha256']
  else:
   count=contract.counters(identity,start_update,parent_validate)
   contract.need(all(type(meta[k])is int and meta[k]==v for k,v in count.items())and op.digest(meta['identity'])==op.digest(identity),'exact continuation boundary')
   contract.validate_history(meta['history'],start_update,identity,parent_validate)
   contract.need(op.state_seal(model,opt)==meta['state_seal'] and op.digest(op.rng_snapshot(torch,np))==meta['rng_state_sha256'],'actual full continuation state/RNG')
   history=list(meta['history']);previous=prior_pair['checkpoint']['sha256']
  for index,scenes in zip(indices,batches):
   stage='UPDATE_%02d'%(index+1);before=op.state_seal(model,opt);rng_before=op.digest(op.rng_snapshot(torch,np));opt.zero_grad(set_to_none=True)
   scene_losses=[];conditions=0;ranking_applied=0
   for scene in scenes:
    modes=op.validate_scene(scene,{'schedule':[[scene['scene_id']]]});losses=[]
    for mode in modes:
     for context in([],scene['given_prefix_tokens']):
      loss,info=admission.condition_loss(mode['graph'],scene['target'],mode['mode'],context,model=model,packed=packed,native_factory=native_factory,adapter=adapter,ranking=ranking,objective=objective)
      contract.need(loss is not None and bool(torch.isfinite(loss)),'no original-ineligible condition silently dropped')
      losses.append(loss);conditions+=1;ranking_applied+=int(info['ranking_applied'])
    contract.need(len(losses)==2*len(modes)and len(losses)in(2,4),'all declared modes/contexts');scene_losses.append(losses)
   loss=admission.scene_condition_mean(scene_losses);loss.backward()
   contract.need(all(p.grad is not None and torch.isfinite(p.grad).all()for p in model.parameters()),'finite complete gradients')
   norm=torch.nn.utils.clip_grad_norm_(model.parameters(),1.0,error_if_nonfinite=True);contract.need(bool(torch.isfinite(norm)),'finite original clip');opt.step()
   contract.need(all(torch.isfinite(p).all()for p in model.parameters()),'finite updated parameters')
   contract.need(not model.embedding.requires_grad and model.embedding.grad is None and op.state_seal(model,opt)['embedding']==before['embedding'],'unchanged frozen embedding')
   contract.need(op.digest(op.rng_snapshot(torch,np))==rng_before,'unchanged update RNG')
   history.append({'logical_update':index+1,'adam_step':27+index,'scenes':len(scenes),'conditions':conditions,'loss':float(loss.detach()),'gradient_norm':float(norm),'ranking_applied_conditions':ranking_applied,'ranking_ineligible_conditions':conditions-ranking_applied})
   stage='CHECKPOINT_%02d'%(index+1);pair=checkpoint.save_pair(root,identity,index+1,history,previous,model,opt,torch,np,contract,parent_validate,op)
   state=op.state_seal(model,opt);rng=op.digest(op.rng_snapshot(torch,np));checkpoint.restore_pair(pair,identity,model,opt,torch,np,contract,parent_validate,op,expected_previous_sha256=previous)
   contract.need(op.state_seal(model,opt)==state and op.digest(op.rng_snapshot(torch,np))==rng,'each checkpoint full restore exact')
   pairs.append({'logical_update':index+1,'pair':pair,'previous_checkpoint_sha256':previous,'state_seal':state,'rng_state_sha256':rng});previous=pair['checkpoint']['sha256']
  contract.need(all(op.file_sha(ref['path'])==ref['sha256']for ref in prior_pair.values()),'prior pair immutable')
  count=contract.counters(identity,start_update+4,parent_validate)
  result={'status':'RANK_REMAINING93_WINDOW_COMPLETE_REMOTE_BACKUP_REQUIRED','identity_sha256':op.digest(identity),**count,'ranking_scenes_including_prior_fork':4+count['scene_cursor'],'ranking_updates_including_prior_fork':1+count['logical_update'],'window_updates':4,'window_index':start_update//4,'start_logical_update':start_update,'end_logical_update':start_update+4,'start_adam_step':26+start_update,'end_adam_step':30+start_update,'start_scene_cursor':contract.counters(identity,start_update,parent_validate)['scene_cursor'],'input_checkpoint_sha256':prior_pair['checkpoint']['sha256'],'history':history,'pairs':pairs,'final_pair':pairs[-1]['pair'],'state_seal':op.state_seal(model,opt),'rng_state_sha256':op.digest(op.rng_snapshot(torch,np)),'remote_backup_verified':False,'next_window_authorized':False,'epoch_complete':count['logical_update']==24}
  op.put(root/'COMPLETE.json',result);return result
 except BaseException as error:
  op.put(root/'UNKNOWN.json',{'stage':stage,'type':type(error).__name__,'automatic_retry':False});raise RuntimeError('RANK_REMAINING93_WINDOW_UNKNOWN')from None
