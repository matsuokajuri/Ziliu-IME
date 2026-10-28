"""Independent stdlib scalar-list AdamW oracle, no NumPy import."""
import math

def transition(parameters,gradients,moments,variances,t,decay_keys):
    norm=math.sqrt(math.fsum(x*x for name in sorted(gradients) for x in gradients[name]))
    factor=min(1.,1./norm) if norm else 1.;p={};m={};v={}
    for key in parameters:
        p[key]=[];m[key]=[];v[key]=[]
        for x,g,a,b in zip(parameters[key],gradients[key],moments[key],variances[key]):
            g*=factor;a=.9*a+.1*g;b=.999*b+.001*g*g
            mh=a/(1-.9**(t+1));vh=b/(1-.999**(t+1))
            x=x*(1-.03*.01) if key in decay_keys else x
            p[key].append(x-.03*mh/(math.sqrt(vh)+1e-8));m[key].append(a);v[key].append(b)
    return p,m,v,t+1
