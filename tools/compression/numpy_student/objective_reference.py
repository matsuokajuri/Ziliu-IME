"""Stdlib design reference only. No model, data loader, optimizer or execution permit."""
import math
WIDTH=128
TEMPERATURE=2.0
GOLD_WEIGHT=0.5

def geometry():
 h=WIDTH
 return {'projection':192*h,'context_gru':3*h*h+3*h*h+6*h,'context_initialized_candidate_gru':3*h*h+3*h*h+6*h,'hidden':(4*h+2)*h+h,'output':h+1,'frozen_embedding':8192*192}

def logsoftmax(values):
 m=max(values);shifted=[x-m for x in values]
 if not all(math.isfinite(x) for x in shifted):raise ValueError('score range exceeds finite arithmetic')
 logz=math.log(sum(math.exp(x) for x in shifted));return [x-logz for x in shifted]

def objective_and_score_gradient(student,teacher,gold_index=None):
 if not 1<=len(student)==len(teacher)<=132:raise ValueError('complete bounded supported pool required')
 if any(type(x) not in (int,float) or not math.isfinite(x) for x in list(student)+list(teacher)):raise ValueError('finite scores')
 if gold_index is not None and (type(gold_index)is not int or not 0<=gold_index<len(student)):raise ValueError('valid training-only gold or absent')
 t=TEMPERATURE
 sl=logsoftmax([s/t for s in student]);tl=logsoftmax([v/t for v in teacher]);sp=[math.exp(x) for x in sl];tp=[math.exp(x) for x in tl]
 loss=t*t*sum(p*(a-b) for p,a,b in zip(tp,tl,sl));grad=[t*(p-q) for p,q in zip(sp,tp)]
 if gold_index is not None:
  logp=logsoftmax(student);loss-=GOLD_WEIGHT*logp[gold_index]
  grad=[g+GOLD_WEIGHT*(math.exp(lp)-(1 if i==gold_index else 0)) for i,(g,lp) in enumerate(zip(grad,logp))]
 return loss,grad
