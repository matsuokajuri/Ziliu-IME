"""Pure in-memory explanation of existing original survival constraints.
No IO/model/import of numeric libraries; does not change the objective.
"""
import math
THRESHOLD=1e-9

def need(v,message):
 if not v:raise ValueError(message)
def number(x):return type(x)in(int,float)and math.isfinite(x)
def reconstruct(graph,search):
 edges,entries,total=graph['edges'],graph['entries'],graph['total'];need(type(total)is int and total>0,'positive total')
 nodes={}
 for row in search['nodes']:
  need(type(row)in(list,tuple)and len(row)==7,'seven node fields')
  node,parent,start,end,edge,score,base=row
  need(all(type(x)is int for x in row[:5])and 1<node and 1<=parent<node and node not in nodes,'unique nodes and ordered parent')
  need(0<=edge<len(edges) and list(edges[edge][:2])==[start,end] and 0<=start<end<=total and number(score)and number(base),'node span/edge')
  nodes[node]=row
 paths={1:()};texts={1:''};ends={1:0}
 for node,row in sorted(nodes.items()):
  _,parent,start,end,edge,_,_=row;need(parent in paths and ends[parent]==start,'closed connected parent chain');entry=entries[edges[edge][2]];need(type(entry['text'])is str and entry['text'],'nonempty edge text')
  paths[node]=paths[parent]+(edge,);texts[node]=texts[parent]+entry['text'];ends[node]=end
 pool=search['pool'];ids=search['pool_ids'];need(len(pool)==len(ids)and len(ids)==len(set(ids)),'exact unique pool node IDs')
 for item,node in zip(pool,ids):
  need(type(node)is int and node in nodes and item['node']==node and ends[node]==total,'pool exact complete node')
  need(item['text']==texts[node],'pool text matches reconstructed edges')
 return [(item['text'],paths[node])for item,node in zip(pool,ids)]

def classify(graph,target,residuals,search,objective,threshold=THRESHOLD):
 """objective must be the caller's pinned original module (PENALTY, selected_boundaries).
 Classification uses edge-prefix identity, not text-prefix or gold segmentation.
 'off_surviving_path' is a descriptive relation, not a bug/causality assertion.
 """
 need(type(target)is str and target and number(threshold)and threshold==THRESHOLD,'fixed reporting threshold')
 edges,entries,total=graph['edges'],graph['entries'],graph['total'];need(len(residuals)==len(edges)and all(number(x)for x in residuals),'finite complete residuals')
 for e in edges:
  need(type(e)in(list,tuple)and len(e)==3 and all(type(x)is int for x in e),'integer edge')
  start,end,k=e;need(0<=start<end<=total and 0<=k<len(entries)and number(entries[k]['weight']),'bounded weighted edge')
 need(number(objective.PENALTY),'original finite penalty')
 surviving=[p for text,p in reconstruct(graph,search)if text==target]
 pairs,counts=objective.selected_boundaries(graph,target,residuals,search)
 selected=[]if pairs is None else list(pairs)
 def score(path):
  need(type(path)in(list,tuple)and bool(path),'nonempty selected prefix');end=0;value=0.
  for i in path:
   need(type(i)is int and 0<=i<len(edges),'selected edge index');s,e,k=edges[i];need(s==end and not(s==0 and e==total),'connected selected prefix, no excluded fullspan');end=e
   value=value+(entries[k]['weight']+objective.PENALTY)+residuals[i]
  need(math.isfinite(value),'finite descriptive score');return value
 rows=[]
 for i,(positive,negative)in enumerate(selected):
  hinge=max(0.,1.+score(negative)-score(positive));need(math.isfinite(hinge),'finite hinge')
  if hinge<=threshold:category='not_positive_above_threshold'
  elif not surviving:category='no_surviving_target'
  elif any(tuple(p[:len(positive)])==tuple(positive)for p in surviving):category='on_surviving_path'
  else:category='off_surviving_path'
  rows.append({'selected_pair_index':i,'descriptive_hinge_float64':hinge,'category':category})
 positive=[r for r in rows if r['descriptive_hinge_float64']>threshold];max_hinge=max([r['descriptive_hinge_float64']for r in rows],default=0.);maxcats=sorted({r['category']for r in positive if r['descriptive_hinge_float64']==max_hinge})
 return {'schema':'RETAINED_TARGET_SELECTED_PATH_CLASSIFICATION_V1','target_retained':bool(surviving),'surviving_target_paths':len(surviving),'dp_reachable':pairs is not None,'selected_pairs':len(rows),'positive_pairs':len(positive),'on_surviving_path_positive_pairs':sum(r['category']=='on_surviving_path'for r in positive),'off_surviving_path_positive_pairs':sum(r['category']=='off_surviving_path'for r in positive),'no_surviving_target_positive_pairs':sum(r['category']=='no_surviving_target'for r in positive),'max_descriptive_hinge_float64':max_hinge,'max_hinge_categories':maxcats,'max_hinge_category':'none_above_threshold'if not maxcats else maxcats[0]if len(maxcats)==1 else 'mixed','reporting_threshold_only':threshold,'pairs':rows,'interpretation':'Edge-path relation only; off-surviving-path does not establish a bug or cause of quality change.'}
