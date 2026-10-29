"""Synthetic-stage ranking supplement; no production/native adapter or data I/O."""
import math

MARGIN = 1.0
RANKING_WEIGHT = 1.0


def need(ok, message):
    if not ok:
        raise ValueError(message)


def validate(graph, residuals):
    import torch
    need(type(graph) is dict, 'graph mapping')
    total = graph['total']
    need(type(total) is int and total > 0, 'positive integer total')
    edges, entries = graph['edges'], graph['entries']
    need(type(edges) in (list, tuple) and len(edges) > 0, 'nonempty edges')
    need(type(entries) in (list, tuple) and len(entries) > 0, 'nonempty entries')
    for entry in entries:
        need(type(entry['text']) is str and bool(entry['text']), 'nonempty entry text')
        need(type(entry['weight']) in (int, float) and math.isfinite(entry['weight']), 'finite weight')
    for edge in edges:
        need(type(edge) in (list, tuple) and len(edge) == 3, 'edge triple')
        start, end, index = edge
        need(all(type(x) is int for x in edge), 'integer edge fields')
        need(0 <= start < end <= total and 0 <= index < len(entries), 'bounded DAG edge')
    need(isinstance(residuals, torch.Tensor) and residuals.ndim == 1 and
         len(residuals) == len(edges) and residuals.is_floating_point(), 'floating edge residual vector')
    need(bool(torch.isfinite(residuals).all()), 'finite residuals')


def path_text(graph, path):
    need(type(path) in (list, tuple) and len(path) > 0, 'nonempty complete edge path')
    offset, parts = 0, []
    for index in path:
        need(type(index) is int and 0 <= index < len(graph['edges']), 'bounded integer edge index')
        start, end, entry = graph['edges'][index]
        need(start == offset, 'connected path from origin')
        offset = end
        parts.append(graph['entries'][entry]['text'])
    need(offset == graph['total'], 'path reaches total')
    return ''.join(parts)


def ranking_loss(graph, target, residuals, candidate_paths, objective):
    """Return (tensor or None, explicit status). Selection is detached and fixed.

    All supplied candidate paths are validated before gold reachability is tested.
    The caller must bind these paths to its exact graph and search result. No claim
    is made that this function verifies native search provenance or completeness.
    """
    import torch
    validate(graph, residuals)
    need(type(target) is str and bool(target), 'nonempty target')
    need(type(candidate_paths) in (list, tuple), 'explicit candidate path collection')
    need(math.isfinite(objective.PENALTY), 'finite original penalty')
    scored = []
    duplicate_gold_text = 0
    for path in candidate_paths:
        text = path_text(graph, path)
        score = objective.differentiable_score(graph, path, residuals)
        need(bool(torch.isfinite(score)), 'finite candidate score')
        if text == target:
            duplicate_gold_text += 1
        else:
            scored.append((float(score.detach()), tuple(path), score))
    gold = objective.gold_path(graph, target, residuals.detach().double().tolist())
    counts = {'candidate_paths': len(candidate_paths), 'same_target_text_paths': duplicate_gold_text,
              'negative_paths': len(scored), 'margin': MARGIN, 'ranking_weight': RANKING_WEIGHT}
    if gold is None:
        return None, dict(counts, status='UNREACHABLE_TARGET', ranking_eligible=False)
    need(path_text(graph, gold) == target, 'original gold path matches target')
    positive = objective.differentiable_score(graph, gold, residuals)
    need(bool(torch.isfinite(positive)), 'finite gold score')
    if not scored:
        return None, dict(counts, status='NO_DIFFERENT_TEXT_NEGATIVE', ranking_eligible=False)
    # Deterministic tie rule; selection is not differentiated.
    negative = min(scored, key=lambda x: (-x[0], x[1]))
    loss = torch.relu(MARGIN + negative[2] - positive)
    need(bool(torch.isfinite(loss)), 'finite ranking loss')
    return loss, dict(counts, status='RANKING_ELIGIBLE', ranking_eligible=True,
                      gold_path=list(gold), negative_path=list(negative[1]),
                      positive_score=float(positive.detach()), negative_score=negative[0])


def combined_condition_loss(graph, target, residuals, search, valid, candidate_paths, objective):
    """Preserve original coverage, with an explicit absent ranking term when skipped."""
    ranking, rank_counts = ranking_loss(graph, target, residuals, candidate_paths, objective)
    original, original_counts = objective.condition_loss(graph, target, residuals, search, valid)
    details = {'ranking': rank_counts, 'survival': original_counts,
               'ranking_applied': False, 'ranking_term': None}
    if original is None:
        return None, dict(details, status='ORIGINAL_OBJECTIVE_INELIGIBLE')
    import torch
    need(bool(torch.isfinite(original)), 'finite original objective')
    if ranking is None:
        return original, dict(details, status='ORIGINAL_ONLY_RANKING_INELIGIBLE')
    total = original + RANKING_WEIGHT * ranking
    need(bool(torch.isfinite(total)), 'finite combined objective')
    return total, dict(details, status='COMBINED_ELIGIBLE', ranking_applied=True,
                       ranking_term=float(ranking.detach()))
