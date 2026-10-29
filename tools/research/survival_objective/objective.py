"""Detached coherent gold DP and actual current-native-frontier survival boundaries."""
from functools import lru_cache
from data import PENALTY

def gold_path(graph,target,residuals):
    total=graph['total'];states={(0,0):(0.,())};by_start={}
    for i,(s,e,k) in enumerate(graph['edges']):
        if not(s==0 and e==total):by_start.setdefault(s,[]).append((i,e,k))
    for start in sorted(by_start):
        parents=[(j,v) for (s,j),v in states.items() if s==start]
        for i,end,k in by_start[start]:
            entry=graph['entries'][k];text=entry['text'];inc=(entry['weight']+PENALTY)+float(residuals[i])
            for char,(score,path) in parents:
                if target.startswith(text,char):
                    key=(end,char+len(text));candidate=score+inc;newpath=path+(i,)
                    if key not in states or candidate>states[key][0] or (candidate==states[key][0] and newpath<states[key][1]):states[key]=(candidate,newpath)
    result=states.get((total,len(target)))
    return result[1] if result is not None else None

def selected_boundaries(graph,target,residuals,search):
    gold=gold_path(graph,target,residuals)
    if gold is None:return None,{'unreachable':1}
    edges=graph['edges'];entries=graph['entries'];total=graph['total'];nodes={n[0]:n for n in search['nodes']}
    @lru_cache(None)
    def path(node):
        if node==1:return ()
        n=nodes[node];return path(n[1])+(n[4],)
    @lru_cache(None)
    def text(node):return ''.join(entries[edges[i][2]]['text'] for i in path(node))
    by_start={}
    for i,(s,e,k) in enumerate(edges):
        if not(s==0 and e==total):by_start.setdefault(s,[]).append((e,entries[k]['text']))
    @lru_cache(None)
    def finish(offset,char):
        if offset==total:return char==len(target)
        return any(target.startswith(t,char) and finish(end,char+len(t)) for end,t in by_start.get(offset,[]))
    def productive(offset,node):
        value=text(node)
        return target.startswith(value) and finish(offset,len(value))
    states=dict(search['states']);pool_has=any(x['text']==target for x in search['pool'])
    counts={'proper_boundaries':0,'terminal_pool_rescued':int(pool_has),'missing_frontier':0,'unsaturated':0,'no_nonproductive':0,'same_text_rescue':0,'selected':0}
    chosen=[];prefix=();prefix_text='';prefix_score=0.
    for i in gold:
        prefix=prefix+(i,);s,end,k=edges[i];prefix_text+=entries[k]['text'];prefix_score+=(entries[k]['weight']+PENALTY)+float(residuals[i])
        if end==total and pool_has:continue
        if end<total:counts['proper_boundaries']+=1
        frontier=states.get(end,[])
        if not frontier:counts['missing_frontier']+=1;continue
        if len(frontier)<24:counts['unsaturated']+=1;continue
        negatives=[node for node in frontier if not productive(end,node)]
        if not negatives:counts['no_nonproductive']+=1;continue
        negative=negatives[-1];positive=prefix
        same=[node for node in frontier if text(node)==prefix_text]
        if same and nodes[same[0]][5]>prefix_score:positive=path(same[0]);counts['same_text_rescue']+=1
        chosen.append((positive,path(negative)));counts['selected']+=1
    return chosen,counts

def differentiable_score(graph,edge_path,residuals):
    import torch
    score=residuals.new_zeros((),dtype=torch.float64)
    for i in edge_path:
        entry=graph['entries'][graph['edges'][i][2]]
        score=score+(residuals[i].double()+(entry['weight']+PENALTY))
    return score

def condition_loss(graph,target,residuals,search,valid):
    import torch
    selected,counts=selected_boundaries(graph,target,residuals.detach().double().tolist(),search)
    if selected is None:return None,counts
    hinges=[torch.relu(1.+differentiable_score(graph,n,residuals)-differentiable_score(graph,p,residuals)) for p,n in selected]
    survive=torch.stack(hinges).max(dim=0).values if hinges else residuals.sum().double()*0.
    regularizer=residuals[valid].square().mean() if valid.any() else residuals.sum()*0.
    return survive+1e-3*regularizer.double(),counts
