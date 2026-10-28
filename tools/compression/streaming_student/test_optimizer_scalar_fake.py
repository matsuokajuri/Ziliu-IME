"""Actual optimizer operations on a stdlib scalar fake, NOT NumPy qualification."""
import contextlib,math,struct,unittest
import adamw_profile_v1 as opt
class Truth:
    def __init__(self,value):self.value=value
    def all(self):return self.value
class A:
    dtype='float64';shape=(1,);size=1
    def __init__(self,x):self.x=float(x)
    @property
    def flat(self):return (self.x,)
    def tobytes(self):return struct.pack('<d',self.x)
    def copy(self):return A(self.x)
    def __add__(self,o):return A(self.x+(o.x if isinstance(o,A)else o))
    __radd__=__add__
    def __sub__(self,o):return A(self.x-(o.x if isinstance(o,A)else o))
    def __mul__(self,o):return A(self.x*(o.x if isinstance(o,A)else o))
    __rmul__=__mul__
    def __truediv__(self,o):return A(self.x/(o.x if isinstance(o,A)else o))
    def __ge__(self,o):return Truth(self.x>=o)
class NP:
    ndarray=A
    dtype=staticmethod(lambda s:s)
    isfinite=staticmethod(lambda a:Truth(math.isfinite(a.x)))
    shares_memory=staticmethod(lambda a,b:a is b)
    zeros_like=staticmethod(lambda a:A(0.))
    sqrt=staticmethod(lambda a:A(math.sqrt(a.x)))
    errstate=staticmethod(lambda **kw:contextlib.nullcontext())
class ScalarTests(unittest.TestCase):
    def test_two_steps_match_independent_scalar_equations(self):
        p={'w':A(1.)};s=opt.initial(NP,p);x=1.;m=v=0.
        for t,g in enumerate((2.5,-.5),1):
            clipped=g*min(1.,1./abs(g));m=.9*m+.1*clipped;v=.999*v+.001*clipped*clipped
            x=x*(1-.001*.01)-.001*(m/(1-.9**t))/(math.sqrt(v/(1-.999**t))+1e-8)
            p,s,_=opt.step(NP,p,{'w':A(g)},s,frozenset({'w'}));self.assertAlmostEqual(p['w'].x,x,places=14)
            self.assertAlmostEqual(s['m']['w'].x,m,places=14);self.assertEqual(s['step'],t)
    def test_no_decay_for_unlisted_parameter(self):
        p={'w':A(2.)};s=opt.initial(NP,p);p2,_,_=opt.step(NP,p,{'w':A(0.)},s,frozenset());self.assertEqual(p2['w'].x,2.)
    def test_profile_mutation_and_alias_rejected(self):
        p={'w':A(2.)};s=opt.initial(NP,p);s['config']['learning_rate']=.03
        with self.assertRaises(ValueError):opt.step(NP,p,{'w':A(1.)},s,frozenset())
        s=opt.initial(NP,p);s['m']['w']=p['w']
        with self.assertRaises(ValueError):opt.step(NP,p,{'w':A(1.)},s,frozenset())
if __name__=='__main__':unittest.main()
