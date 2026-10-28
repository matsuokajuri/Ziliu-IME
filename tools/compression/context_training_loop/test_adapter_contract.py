"""Backend wiring only; fake scalars, no NumPy or actual model execution."""
import ast
import types
import unittest
from pathlib import Path
from numpy_adapter import NumpyBackend

class AdapterTests(unittest.TestCase):
    def fixture(self):
        self.calls=[]
        def pool(np,gru,objective,p,context,candidates,teacher,gold):
            self.calls.append(('pool',len(candidates),len(teacher),gold))
            return 4.0,{'p':2.0},[0.0]*len(candidates)
        def step(np,p,g,opt,decay):
            self.calls.append(('step',dict(g),decay));return {'p':p['p']-g['p']},{**opt,'step':opt['step']+1},{}
        def forward(np,gru,p,context,candidates):
            self.calls.append(('forward',len(candidates)));return [1.0]*len(candidates),None
        backend=NumpyBackend(types.SimpleNamespace(zeros_like=lambda v:0.0),types.SimpleNamespace(pool_loss_and_gradients=pool,forward=forward),None,None,types.SimpleNamespace(step=step))
        backend.thaw=lambda x:dict(x);backend.freeze=lambda x:dict(x)
        state={'parameters':{'p':5.0},'optimizer':{'step':0,'config':{},'m':{'p':0.0},'v':{'p':0.0}},'rng_state':[], 'cursor':{'completed_updates':0,'next_batch':0}}
        records=[{'context':[],'candidates':[1,2,3],'teacher_scores':[3,2,1],'gold_index':None} for _ in range(4)]
        return backend,state,records
    def test_full_pools_average_then_one_update(self):
        b,s,r=self.fixture();new,metrics=b.train(s,r,0.25,{'decay_keys':['p']})
        self.assertEqual(self.calls[:4],[('pool',3,3,None)]*4)
        self.assertEqual(self.calls[4:], [('step',{'p':2.0},frozenset({'p'}))])
        self.assertEqual(new['parameters']['p'],3.0);self.assertEqual(metrics,{'batch_loss':4.0,'pool_count':4})
        self.assertEqual(s['parameters']['p'],5.0)
    def test_dev_only_forward(self):
        b,s,r=self.fixture();result=b.evaluate(s,[{'id':'dev','context':[],'candidates':[1,2]}],{})
        self.assertEqual(self.calls,[('forward',2)]);self.assertEqual(result,[{'id':'dev','scores':[1.,1.]}])
    def test_no_backend_import_or_launcher(self):
        for filename in ['training_loop.py','numpy_adapter.py']:
            tree=ast.parse((Path(__file__).parent/filename).read_text())
            for n in ast.walk(tree):
                if isinstance(n,ast.Import):self.assertFalse(any(a.name.split('.')[0] in ('numpy','torch') for a in n.names))
                if isinstance(n,ast.ImportFrom):self.assertNotIn(n.module,('numpy','torch'))

if __name__=='__main__':unittest.main()
