"""Independent scalar stdlib oracle; synthetic finite-difference gold only."""
import copy,hashlib,json,math
from pathlib import Path
H,D,EPS=2,3,1e-5
def sigmoid(x):
 if x>=0:return 1/(1+math.exp(-x))
 e=math.exp(x);return e/(1+e)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cell(x,h,p):
 ai=[dot(row,x)+b for row,b in zip(p['w_input'],p['b_input'])];ah=[dot(row,h)+b for row,b in zip(p['w_recurrent'],p['b_recurrent'])]
 r=[sigmoid(ai[j]+ah[j]) for j in range(H)];z=[sigmoid(ai[H+j]+ah[H+j]) for j in range(H)]
 n=[math.tanh(ai[2*H+j]+r[j]*ah[2*H+j]) for j in range(H)]
 return [(1-z[j])*n[j]+z[j]*h[j] for j in range(H)]
def sequence(xs,h,p):
 states=[]
 for x in xs:h=cell(x,h,p);states.append(h)
 return states
def params(offset):
 k=offset
 def values(count):
  nonlocal k
  out=[.19*math.sin((k+i+1)*.71) for i in range(count)];k+=count;return out
 return {'w_input':[values(D) for _ in range(3*H)],'w_recurrent':[values(H) for _ in range(3*H)],'b_input':values(3*H),'b_recurrent':values(3*H)}
def payload(chain):
 p={'context_parameters':params(0),'context_inputs':[[-.7,.2,.1],[.5,-.4,.3],[.1,.8,-.6]],'context_h0':[-.2,.35]}
 if chain:p.update(candidate_parameters=params(47),candidate_inputs=[[.4,-.8,.6],[-.1,.2,.9]])
 return p
def loss(p):
 c=sequence(p['context_inputs'],p['context_h0'],p['context_parameters']);v=sum(.17*(i+1)*h[0]-.11*(i+2)*h[1] for i,h in enumerate(c))
 if 'candidate_parameters' in p:v+=sum(.31*(i+1)*h[0]+.23*(i+1)*h[1] for i,h in enumerate(sequence(p['candidate_inputs'],c[-1],p['candidate_parameters'])))
 return v
def leaves(v,path=()):
 if isinstance(v,dict):
  for k in sorted(v):yield from leaves(v[k],path+(k,))
 elif isinstance(v,list):
  for i,x in enumerate(v):yield from leaves(x,path+(i,))
 else:yield path
def at(v,path):
 for k in path:v=v[k]
 return v
def put(v,path,x):at(v,path[:-1])[path[-1]]=x
def gradients(p,eps):
 out=[]
 for path in leaves(p):
  a=copy.deepcopy(p);b=copy.deepcopy(p);x=at(p,path);put(a,path,x+eps);put(b,path,x-eps);out.append({'path':list(path),'value':(loss(a)-loss(b))/(2*eps)})
 return out
def fixture():
 cases=[]
 for chain in (False,True):
  p=payload(chain);g=gradients(p,EPS);g2=gradients(p,EPS/2);delta=max(abs(a['value']-b['value']) for a,b in zip(g,g2))
  if delta>1e-7:raise RuntimeError('finite difference step instability')
  cases.append({'name':'context_candidate_chain' if chain else 'context_sequence','inputs_and_parameters':p,'loss':loss(p),'gradients':g,'gradient_count':len(g),'epsilon':EPS,'half_step_max_abs_difference':delta})
 return {'schema':'INDEPENDENT_STDLIB_GRU_FLOAT64_SYNTHETIC_V1','hidden_width':H,'input_width':D,'reset_variant':'after recurrent affine including candidate recurrent bias','source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'cases':cases,'atol':1e-7,'rtol':1e-5,'numpy_calls':0,'model_calls':0,'quality_claim':False}
if __name__=='__main__':
 s=json.dumps(fixture(),sort_keys=True,indent=2,allow_nan=False)+'\n'
 with Path(__file__).with_name('INDEPENDENT-GRU-SYNTHETIC-FIXTURE.json').open('x') as f:f.write(s)
 print(hashlib.sha256(s.encode()).hexdigest())
