"""Candidate-source contract only. No build004 provenance or native integration.
Pure in-memory validation: no data reads, model imports, or native execution.
"""
import math

PENALTY = -13.815510557964274
ATOL = 1e-10
RTOL = 1e-12


def need(ok, message):
    if not ok:
        raise ValueError(message)


def number(x):
    return type(x) in (int, float) and math.isfinite(x)


def same(a, b):
    return number(a) and number(b) and math.isclose(a, b, abs_tol=ATOL, rel_tol=RTOL)


def adapt_candidate_paths(graph, residuals, snapshot):
    """Return ordered full edge paths and checks, never a native-admission proof.

    Contract: implicit root 1; seven-field nodes; grammar/LLR off; no extra
    bias; full-span edges excluded; node weights learned then base. Dictionary
    and graph order must already be bound by an independent native admission.
    """
    need(type(graph) is dict and type(snapshot) is dict, 'mapping inputs')
    total = graph['total']
    edges, entries = graph['edges'], graph['entries']
    need(type(total) is int and 0 < total <= 256, 'bounded total')
    need(type(edges) in (list, tuple) and type(entries) in (list, tuple)
         and len(edges) <= 65536 and len(entries) <= 65536, 'bounded graph')
    need(type(residuals) in (list, tuple) and len(residuals) == len(edges)
         and all(number(x) for x in residuals), 'finite residual vector')
    for entry in entries:
        need(type(entry) is dict and type(entry['text']) is str and entry['text']
             and number(entry['weight']), 'entry fields')
    seen_edges = set()
    previous = (-1, -1)
    for edge in edges:
        need(type(edge) in (list, tuple) and len(edge) == 3
             and all(type(x) is int for x in edge), 'integer edge triple')
        start, end, index = edge
        need(0 <= start < end <= total and 0 <= index < len(entries), 'edge bounds')
        need((start, end) >= previous and tuple(edge) not in seen_edges, 'original edge order/identity')
        previous = (start, end)
        seen_edges.add(tuple(edge))
    rows = snapshot['nodes']
    attempts = snapshot['attempts']
    need(type(attempts) is int and 0 <= attempts <= 131072, 'attempt count')
    need(type(rows) in (list, tuple) and len(rows) <= attempts, 'bounded nodes')
    nodes = {}
    for row in rows:
        need(type(row) in (list, tuple) and len(row) == 7, 'seven node fields')
        node, parent, start, end, edge, score, base = row
        need(all(type(x) is int for x in row[:5]), 'integer node fields')
        need(2 <= node <= attempts + 1 and 1 <= parent < node and node not in nodes,
             'unique node and earlier parent')
        need(0 <= edge < len(edges) and number(score) and number(base), 'node edge and finite weights')
        need([start, end] == list(edges[edge][:2]) and not (start == 0 and end == total),
             'node span and forbidden full-span')
        nodes[node] = row
    paths = {1: ()}
    texts = {1: ''}
    scores = {1: (0.0, 0.0)}
    ends = {1: 0}
    for node, row in sorted(nodes.items()):
        _, parent, start, end, edge, score, base = row
        need(parent in paths and ends[parent] == start, 'parent closure and connected span')
        entry = entries[edges[edge][2]]
        increment = entry['weight'] + PENALTY
        expected_base = scores[parent][1] + increment
        expected_score = scores[parent][0] + (increment + residuals[edge])
        need(same(base, expected_base) and same(score, expected_score), 'cumulative node scores')
        paths[node] = paths[parent] + (edge,)
        texts[node] = texts[parent] + entry['text']
        # Use recomputed values, so repeated tolerance cannot drift cumulatively.
        scores[node] = (expected_score, expected_base)
        ends[node] = end
    pool, ids = snapshot['pool'], snapshot['pool_ids']
    need(type(pool) in (list, tuple) and type(ids) in (list, tuple)
         and len(pool) == len(ids) <= 64 and len(ids) == len(set(ids)), 'pool size/identity')
    output = []
    seen_texts = set()
    for item, node in zip(pool, ids):
        need(type(node) is int and node in nodes and item['node'] == node
             and type(item['node']) is int, 'ordered exact pool identity')
        need(ends[node] == total and item['start'] == 0 and item['end'] == total
             and type(item['start']) is int and type(item['end']) is int, 'complete pool span')
        need(item['text'] == texts[node] and texts[node] not in seen_texts, 'exact unique pool text')
        need(same(item['weight'], scores[node][1])
             and same(item['retained_weight'], scores[node][1]), 'pool weights must be base, not learned')
        lengths = [edges[e][1] - edges[e][0] for e in paths[node]]
        need(item['word_lengths'] == lengths and all(type(x) is int for x in item['word_lengths']), 'pool segmentation')
        seen_texts.add(texts[node])
        output.append(paths[node])
    # All exposed state/best references must bind to reconstructed nodes.
    offsets = set()
    for offset, refs in snapshot['states']:
        need(type(offset) is int and 0 <= offset <= total and offset not in offsets, 'state offset')
        offsets.add(offset)
        need(len(refs) == len(set(refs)), 'unique state refs')
        for node in refs:
            need(type(node) is int and node in paths and ends[node] == offset, 'state node span')
    need(len(snapshot['best']) == len(set(snapshot['best'])), 'unique best refs')
    for node in snapshot['best']:
        need(type(node) is int and node in nodes and ends[node] == total, 'terminal best node')
    return output, {'status': 'CANDIDATE_SOURCE_CONTRACT_VALIDATED_ONLY',
                    'nodes_checked': len(nodes), 'pool_paths_checked': len(output),
                    'score_atol': ATOL, 'score_rtol': RTOL, 'full_span_allowed': False,
                    'extra_bias': 0, 'build004_provenance_verified': False,
                    'native_integration_admitted': False}
