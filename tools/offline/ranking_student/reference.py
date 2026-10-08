"""Small stdlib loss/gradient oracle; these checks are not tensor/model training."""
import math
from .contracts import finite_vector


def softmax(scores):
    finite_vector(scores, len(scores))
    if not scores:
        raise ValueError("nonempty score vector required")
    top = max(scores)
    z = [math.exp(s-top) for s in scores]
    return [x/sum(z) for x in z]


def objective_and_gradient(scores, target, *, kind="teacher_distribution", pair_weight=0.2):
    """T=1: listwise CE or set probability, plus normalized soft pair logistic."""
    p = softmax(scores)
    n = len(scores)
    if kind == "teacher_distribution":
        finite_vector(target, n)
        if any(x < 0 for x in target) or abs(sum(target)-1) > 1e-6:
            raise ValueError("probability target required")
        # logsumexp avoids underflow from log(softmax).
        m = max(scores)
        lse = m + math.log(sum(math.exp(s-m) for s in scores))
        mass = sum(target)  # Preserve tolerated floating-point normalization error.
        loss = mass*lse-sum(q*s for q, s in zip(target, scores))
        grad = [mass*x-y for x, y in zip(p, target)]
        pairs = [(i,j,target[i]/(target[i]+target[j])) for i in range(n) for j in range(i+1,n)
                 if target[i]+target[j] > 0]
    elif kind == "acceptable_set":
        if not target or len(set(target)) != len(target) or any(type(i) is not int or not 0 <= i < n for i in target):
            raise ValueError("nonempty acceptable offsets required")
        a = softmax([scores[i] for i in target])
        m, ma = max(scores), max(scores[i] for i in target)
        loss = m+math.log(sum(math.exp(s-m) for s in scores))-ma-math.log(sum(math.exp(scores[i]-ma) for i in target))
        grad = p[:]
        for i, value in zip(target, a):
            grad[i] -= value
        pairs = [(i,j,1.0) for i in target for j in range(n) if j not in target]
    else:
        raise ValueError("known target kind required")
    for i, j, q in pairs:
        delta = scores[i]-scores[j]
        factor = pair_weight/len(pairs)
        loss += factor*(max(delta,0)+math.log1p(math.exp(-abs(delta)))-q*delta)
        sigmoid = 1/(1+math.exp(-delta)) if delta >= 0 else math.exp(delta)/(1+math.exp(delta))
        g = factor*(sigmoid-q)
        grad[i] += g
        grad[j] -= g
    return loss, grad


def ranked_source_indices(ids, scores):
    finite_vector(scores, len(ids))
    if len(set(ids)) != len(ids):
        raise ValueError("unique source identity required")
    return [ids[i] for i in sorted(range(len(ids)), key=lambda i:(-scores[i],ids[i]))]
