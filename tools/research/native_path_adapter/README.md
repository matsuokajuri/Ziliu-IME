# Candidate native-path adapter — synthetic stage only

`adapter.py` accepts in-memory graph, detached numeric residuals and a search
snapshot, and returns complete edge-index tuples in original candidate-pool
order. It performs no file reads, native calls, model imports or training.

The candidate contract follows inspected source with implicit root 1 and node
rows `[id,parent,start,end,edge_index,search_weight,base_weight]`. Node arrays may
be reordered or sparse, but every parent must exist and have an earlier ID.
All edges must connect, and pool paths must end at total. Full-span edges are
forbidden in reconstructed paths. Empty states are supported, as in the v3
candidate. Graph entry ordering and snapshot provenance remain external gates.

The pool's `weight` and `retained_weight` are **base** weights, not learned search
weights. Learned weights come from node column 5. The adapter recomputes both
with null grammar, LLR off, penalty `-13.815510557964274`, and no additional bias.
It preserves cumulative arithmetic grouping and uses fixed abs tolerance
1e-10 plus relative tolerance 1e-12. It does not tune tolerance on real data.

Text, segmentation and numeric weight checks do not validate every native
sentence field (for example code, type and components). They are sufficient
only for the declared complete-path ranking interface, not complete ABI
certification. Candidate order is preserved, not claimed globally sorted.
Checking output consistency cannot prove a binary was compiled from a source.

The exact source-to-binary build chain remains unverified here. Every result explicitly says
`native_integration_admitted=False`; this module is not yet wired to real
training or evaluation. Synthetic tests do not certify real native execution,
Windows operation, final quality, or numerical recovery.

Run `python -B -m unittest discover -s <this-directory> -p 'test_adapter.py' -v`.
