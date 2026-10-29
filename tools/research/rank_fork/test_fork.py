"""Invented toy states only: no real model, dataset, native library or checkpoint."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest


def load(name, path):
    spec=importlib.util.spec_from_file_location(name,path);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod


def parent_validate(parent):
    assert parent['schema']=='PUBLIC_HEAD_FIXED97_EPOCH_V1'
    assert len(parent['schedule'])==25
    return parent['schedule']


def history():
    return [{'logical_update':1,'adam_step':26,'scenes':4,'conditions':8,'loss':1.,
             'gradient_norm':.5,'ranking_applied_conditions':6,'ranking_ineligible_conditions':2}]


class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        class Toy(torch.nn.Module):
            def __init__(self):
                super().__init__();self.linear=torch.nn.Linear(2,1);self.register_buffer('embedding',torch.ones(3,2));self.index={}
            def forward(self):return self.linear(torch.tensor([[1.,2.]]))
        self.model=Toy();self.opt=torch.optim.AdamW(self.model.parameters(),lr=.001,betas=(.9,.999),eps=1e-8,weight_decay=0)
        self.model().square().sum().backward();self.opt.step();self.opt.zero_grad(set_to_none=True)
        for p in self.model.parameters():self.opt.state[p]['step'].fill_(25)
        schedule=[['s0']]+[[f's{1+4*i+j}' for j in range(4)] for i in range(24)]
        parent={'schema':'PUBLIC_HEAD_FIXED97_EPOCH_V1','schedule':schedule}
        self.identity={'schema':C.SCHEMA,'namespace':C.NAMESPACE,'parent_identity':parent,
          'parent_identity_sha256':C.digest(parent),'parent_checkpoint_sha256':'1'*64,
          'parent_rng_file_sha256':'2'*64,'parent_state_sha256':OP.digest(OP.state_seal(self.model,self.opt)),
          'parent_rng_state_sha256':OP.digest(OP.rng_snapshot(torch,np)), 'schedule':[schedule[2]],
          'source_sha256':'3'*64,'selection_review_sha256':'4'*64,'objective':copy.deepcopy(C.OBJECTIVE),
          'optimizer':copy.deepcopy(C.OPTIMIZER),'parent_adam_step':25,'maximum_updates':1}

    def save(self,step=0,rows=None,prev=None,root=None):
        return CP.save_pair(self.root if root is None else root,self.identity,step,[] if rows is None else rows,
          self.identity['parent_checkpoint_sha256'] if prev is None else prev,self.model,self.opt,torch,np,C,parent_validate,OP)

    def restore(self,pair,identity=None,prev=None,op=None):
        return CP.restore_pair(pair,self.identity if identity is None else identity,self.model,self.opt,torch,np,C,parent_validate,OP if op is None else op,
              expected_previous_sha256=self.identity['parent_checkpoint_sha256'] if prev is None else prev)

    def snapshot(self):return OP.state_seal(self.model,self.opt),OP.digest(OP.rng_snapshot(torch,np))

    def rewrite(self,pair,change_meta=lambda x:None,change_side=lambda x:None,change_arrays=lambda x:None):
        with np.load(pair['checkpoint']['path'],allow_pickle=False) as z:arrays={k:np.array(z[k],copy=True) for k in z.files}
        meta=json.loads(str(arrays['metadata']));side=json.loads(Path(pair['rng']['path']).read_text())
        change_meta(meta);change_side(side);change_arrays(arrays)
        meta['rng_state_sha256']=OP.digest(side['rng']);arrays['metadata']=np.array(json.dumps(meta,sort_keys=True))
        dest=self.root/('tamper-'+str(len(list(self.root.glob('tamper-*')))));dest.mkdir()
        cp=dest/'checkpoint.npz';np.savez(cp,**arrays);side['checkpoint_file_sha256']=OP.file_sha(cp)
        rp=dest/'rng.json';rp.write_text(json.dumps(side))
        return {k:{'path':str(p),'bytes':p.stat().st_size,'sha256':OP.file_sha(p)} for k,p in [('checkpoint',cp),('rng',rp)]}

    def test_objective_bool_is_not_margin_float(self):
        self.identity['objective']['ranking_margin']=True
        with self.assertRaises(ValueError):C.validate(self.identity,parent_validate)

    def test_optimizer_parameter_order_is_bound(self):
        self.opt.param_groups[0]['params'].reverse()
        with self.assertRaises(ValueError):self.save()
        self.assertFalse((self.root/'fork-000.npz').exists())

    def test_self_consistent_wrong_migration_rejected(self):
        pair=self.save();before=self.snapshot();name=list(dict(self.model.named_parameters()))[-1]
        changed=dict(self.model.named_parameters())[name].detach().numpy().copy()+1
        value=CP.array_seal(changed)
        bad=self.rewrite(pair,change_meta=lambda m:m['state_seal']['parameters'].__setitem__(name,value),
            change_side=lambda s:s['state_seal']['parameters'].__setitem__(name,value),
            change_arrays=lambda a:a['w/'+name].__iadd__(1))
        with self.assertRaises(ValueError):self.restore(bad)
        self.assertEqual(self.snapshot(),before)

    def test_rng_preflight_is_global_state_free(self):
        before=self.snapshot();rng=copy.deepcopy(OP.rng_snapshot(torch,np));rng['torch_cpu']='AA=='
        with self.assertRaises((RuntimeError,ValueError)):CP.preflight_rng(rng,torch,np,C,OP)
        self.assertEqual(self.snapshot(),before)

    def test_migration_logical0_adam25_restores(self):
        before=self.snapshot();pair=self.save()
        with torch.no_grad():next(self.model.parameters()).add_(100)
        meta=self.restore(pair)
        self.assertEqual((meta['logical_update'],meta['adam_step'],len(meta['history'])),(0,25,0))
        self.assertEqual(self.snapshot(),before)

    def test_update1_adam26_history1_restores(self):
        base=self.save();self.model().square().sum().backward();self.opt.step()
        pair=self.save(1,history(),base['checkpoint']['sha256']);before=self.snapshot()
        with torch.no_grad():next(self.model.parameters()).mul_(0)
        meta=self.restore(pair,prev=base['checkpoint']['sha256'])
        self.assertEqual((meta['logical_update'],meta['adam_step'],len(meta['history'])),(1,26,1));self.assertEqual(self.snapshot(),before)

    def test_wrong_namespace_no_mutation(self):
        pair=self.save();bad=copy.deepcopy(self.identity);bad['namespace']='OLD_EPOCH';before=self.snapshot()
        with self.assertRaises(ValueError):self.restore(pair,bad)
        self.assertEqual(self.snapshot(),before)

    def test_wrong_predecessor_no_mutation(self):
        pair=self.save();before=self.snapshot()
        with self.assertRaises(ValueError):self.restore(pair,prev='9'*64)
        self.assertEqual(self.snapshot(),before)

    def test_global_step_not_logical_step(self):
        before=self.snapshot()
        for p in self.model.parameters():self.opt.state[p]['step'].fill_(0)
        self.identity['parent_state_sha256']=OP.digest(OP.state_seal(self.model,self.opt))
        with self.assertRaises(ValueError):self.save()
        self.assertFalse((self.root/'fork-000.npz').exists())

    def test_history_is_logical_not_global_length(self):
        with self.assertRaises(ValueError):self.save(rows=[{}]*25)
        self.assertFalse((self.root/'fork-000.npz').exists())

    def test_bad_variance_rejected_before_save(self):
        next(iter(self.opt.state.values()))['exp_avg_sq'].fill_(-1)
        self.identity['parent_state_sha256']=OP.digest(OP.state_seal(self.model,self.opt))
        with self.assertRaises(ValueError):self.save()
        self.assertFalse((self.root/'fork-000.npz').exists())

    def test_late_array_tamper_no_partial_mutation(self):
        pair=self.save();before=self.snapshot()
        key='w/'+list(dict(self.model.named_parameters()))[-1]
        bad=self.rewrite(pair,change_arrays=lambda a:a[key].__iadd__(1))
        with self.assertRaises(ValueError):self.restore(bad)
        self.assertEqual(self.snapshot(),before)

    def test_malformed_rng_no_partial_mutation(self):
        pair=self.save();before=self.snapshot()
        bad=self.rewrite(pair,change_side=lambda s:s['rng']['numpy'].__setitem__('keys',[1]))
        with self.assertRaises(ValueError):self.restore(bad)
        self.assertEqual(self.snapshot(),before)

    def test_bad_adam_counter_restore_no_mutation(self):
        pair=self.save();before=self.snapshot();key='o/'+next(iter(dict(self.model.named_parameters())))+'/step'
        bad=self.rewrite(pair,change_arrays=lambda a:a[key].fill(1))
        with self.assertRaises(ValueError):self.restore(bad)
        self.assertEqual(self.snapshot(),before)

    def test_application_failure_rolls_back(self):
        pair=self.save()
        with torch.no_grad():next(self.model.parameters()).add_(7)
        before=self.snapshot();calls=[]
        def broken(rng,t,n):
            OP.restore_rng(rng,t,n);calls.append(1)
            if len(calls)==1:raise RuntimeError('invented application failure')
        proxy=types.SimpleNamespace(**{k:getattr(OP,k) for k in ('digest','state_seal','rng_snapshot','file_sha')},restore_rng=broken)
        with self.assertRaises(RuntimeError):self.restore(pair,op=proxy)
        self.assertEqual(self.snapshot(),before);self.assertEqual(len(calls),2)

    def test_immutable_pair_no_overwrite(self):
        pair=self.save();old=Path(pair['checkpoint']['path']).read_bytes()
        with self.assertRaises(ValueError):self.save()
        self.assertEqual(Path(pair['checkpoint']['path']).read_bytes(),old)

    def operation(self,root,fail=False):
        calls=[];model=self.model
        def condition(*args,**kwargs):
            calls.append(1)
            if fail and len(calls)==2:raise ValueError('invented condition failure')
            return model().square().sum(),{'ranking_applied':len(calls)%2==0}
        def mean(rows):return torch.stack([torch.stack(row).mean() for row in rows]).mean()
        admission=types.SimpleNamespace(condition_loss=condition,scene_condition_mean=mean)
        scenes=[{'scene_id':sid,'target':'invented','given_prefix_tokens':[3],
            'modes':[{'mode':'pureinitial','graph':{}}]} for sid in self.identity['schedule'][0]]
        return F.run(root,self.identity,scenes,model=self.model,opt=self.opt,torch=torch,np=np,packed=None,
             admission=admission,adapter=None,ranking=types.SimpleNamespace(MARGIN=1.,RANKING_WEIGHT=1.),objective=None,
             native_factory=None,contract=C,checkpoint=CP,parent_validate=parent_validate,
             parent_restore=lambda:{'step':25,'scene_cursor':97},op=OP)

    def test_one_complete_operation_and_restore(self):
        result=self.operation(self.root/'operation')
        self.assertEqual((result['logical_update'],result['adam_step'],result['history'][0]['conditions']),(1,26,8))
        self.assertFalse(result['next_update_authorized']);self.assertFalse(result['remote_backup_verified'])

    def test_failure_unknown_and_no_replay(self):
        root=self.root/'failed'
        with self.assertRaises(RuntimeError):self.operation(root,True)
        self.assertTrue((root/'UNKNOWN.json').is_file());self.assertTrue((root/'fork-000.npz').is_file())
        self.assertFalse((root/'fork-001.npz').exists())
        with self.assertRaises(FileExistsError):self.operation(root,False)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--original-operation',required=True);args=parser.parse_args()
    root=Path(__file__).parent
    C=load('fork_contract',root/'fork_contract.py');CP=load('fork_checkpoint',root/'fork_checkpoint.py');F=load('fork_operation',root/'fork_operation.py');OP=load('original_operation',args.original_operation)
    import torch,numpy as np
    torch.set_num_threads(2);torch.set_num_interop_threads(1)
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    print(json.dumps({'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'invented_toy_only':True,'real_model_or_data_loaded':False,'real_training_updates':0}))
    sys.exit(0 if result.wasSuccessful() else 1)
