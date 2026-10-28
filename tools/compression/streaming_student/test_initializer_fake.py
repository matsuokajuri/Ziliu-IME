"""Initializer control flow uses fake RNG/arrays; never actual PCG/NumPy draws."""
import copy,math,types,unittest
from unittest.mock import patch
import initializer as init
class Arr:
    def __init__(self,shape):self.shape=shape
    def astype(self,dtype):return self
class Bit:
    def __init__(self,seed):self.state={'bit_generator':'PCG64','state':{'state':seed,'inc':3},'has_uint32':0,'uinteger':0}
class Generator:
    def __init__(self,bit,calls):self.bit_generator=bit;self.calls=calls
    def uniform(self,low,high,size):
        self.calls.append((low,high,size));self.bit_generator.state['state']['state']+=math.prod(size);return Arr(size)
def shapes(d,h):
    out={'projection':(d,h),'head_weight':(4*h+2,h),'head_bias':(h,),'output_weight':(h,),'output_bias':(1,)}
    for p in ('context','candidate'):
        for k,s in zip(('weight_ih','weight_hh','bias_ih','bias_hh'),((3*h,h),(3*h,h),(3*h,),(3*h,))):out[p+'_'+k]=s
    return out
class InitializerTests(unittest.TestCase):
    def test_fixed_shape_order_seed_and_full_rng_state(self):
        calls=[];np=types.SimpleNamespace(float64='float64',zeros=lambda s,dtype:Arr(s),random=types.SimpleNamespace(PCG64=Bit,Generator=lambda b:Generator(b,calls)))
        model=types.SimpleNamespace(shapes=shapes,parameter_count=lambda d,h:sum(math.prod(s)for s in shapes(d,h).values()),check_parameters=lambda *a:None)
        optimizer=types.SimpleNamespace(initial=lambda n,p:{'config':{'fake':1.},'m':dict(p),'v':dict(p)})
        with patch.object(init,'freeze',side_effect=lambda n,a:{k:list(v.shape)for k,v in a.items()}):state=init.initialize(np,model,optimizer)
        self.assertEqual(state['rng_state']['draw_count'],288640);self.assertEqual(state['rng_state']['initialization_before']['state']['state'],20261028)
        self.assertEqual(state['rng_state']['generator_state']['state']['state'],20261028+288640);self.assertEqual(len(calls),11)
        self.assertEqual([x[2]for x in calls],[s for k,s in sorted(shapes(192,128).items())if k not in ('head_bias','output_bias')])
        self.assertEqual(state['cursor'],{'completed_updates':0,'next_batch':0});self.assertNotIn('E',state)
    def test_seed_must_be_exact_uint64(self):
        for x in (True,-1,2**64,1.0):
            with self.subTest(x=x),self.assertRaises(ValueError):init.initialize(None,None,None,x)
    def test_profile_is_fixed_and_not_a_runtime_permission(self):
        self.assertEqual(init.PROFILE['embedding_width'],192);self.assertEqual(init.PROFILE['hidden_width'],128);self.assertFalse(init.PROFILE['training_rng_consumed'])
if __name__=='__main__':unittest.main()
