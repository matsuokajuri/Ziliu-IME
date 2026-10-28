"""Independent scalar head equations for synthetic qualification, stdlib only."""
import math

def forward(c,states,w,b,v,o,context_length):
    h=len(c);last=states[-1];mean=[sum(row[j] for row in states)/len(states) for j in range(h)]
    f=list(c)+list(last)+mean+[x*y for x,y in zip(c,last)]+[len(states)/63.0,context_length/64.0]
    a=[math.tanh(sum(f[i]*w[i][j] for i in range(len(f)))+b[j]) for j in range(h)]
    return sum(x*y for x,y in zip(a,v))+o,(f,a)

def backward(c,states,w,b,v,o,context_length):
    _,(f,a)=forward(c,states,w,b,v,o,context_length);h=len(c)
    dz=[v[j]*(1-a[j]*a[j]) for j in range(h)]
    df=[sum(w[i][j]*dz[j] for j in range(h)) for i in range(len(f))]
    dc=[df[j]+df[3*h+j]*states[-1][j] for j in range(h)]
    dstates=[[df[2*h+j]/len(states) for j in range(h)] for row in states]
    for j in range(h):dstates[-1][j]+=df[h+j]+df[3*h+j]*c[j]
    return {'c':dc,'states':dstates,'w':[[f[i]*dz[j] for j in range(h)] for i in range(len(f))],'b':dz,'v':a,'o':1.0}
