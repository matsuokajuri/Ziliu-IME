"""Experimental target-coverage gate, not adopted into any trained model.

Keep the original unreachable condition and regularizer. If any native candidate
already matches the target, remove internal-path survival constraints. This
intentionally changes stability-margin behavior; it is not a quality guarantee.
The original trusted objective is injected explicitly; native search is unchanged.
"""
def selected_boundaries(graph,target,residuals,search,original):
    chosen,counts=original.selected_boundaries(graph,target,residuals,search)
    counts=dict(counts)
    counts['coverage_gate_applied']=False
    counts['original_selected']=len(chosen) if chosen is not None else 0
    if chosen is not None and any(x['text']==target for x in search['pool']):
        chosen=[]
        counts['selected']=0
        counts['coverage_gate_applied']=True
    return chosen,counts

def condition_loss(graph,target,residuals,search,valid,original):
    import torch
    chosen,counts=selected_boundaries(graph,target,residuals.detach().double().tolist(),search,original)
    if chosen is None:return None,counts
    hinges=[torch.relu(1.+original.differentiable_score(graph,n,residuals)-original.differentiable_score(graph,p,residuals))for p,n in chosen]
    survive=torch.stack(hinges).max(dim=0).values if hinges else residuals.sum().double()*0.
    regularizer=residuals[valid].square().mean()if valid.any()else residuals.sum()*0.
    return survive+1e-3*regularizer.double(),counts
