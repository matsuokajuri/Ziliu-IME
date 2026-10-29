"""One explicitly injected condition; no optimizer, persistence or data loading."""

def need(ok, message):
    if not ok:
        raise ValueError(message)


def condition_loss(graph, target, mode, context_tokens, *, model, packed,
                   native_factory, adapter, ranking, objective):
    """Run one forward/search, strictly validate paths, preserve original loss.

    Caller owns exact source/material admission, native/library identity, state and
    RNG seals, resource lease and any autograd mode. No backward or optimizer call
    occurs here. Returned metadata deliberately excludes texts, paths, and inputs.
    """
    import torch
    need(mode in ('pureinitial', 'mixed'), 'original supported mode')
    need(type(context_tokens) in (list, tuple) and len(context_tokens) <= 64 and
         all(type(x) is int and 1 <= x < 8192 and x != 2 for x in context_tokens), 'explicit context tokens')
    prepared = packed.prepare_graph(graph, mode, model.index, context_tokens)
    residuals = model(prepared, context_tokens)
    need(isinstance(residuals, torch.Tensor) and residuals.ndim == 1 and
         len(residuals) == len(graph['edges']) and torch.isfinite(residuals).all(),
         'finite complete model residual vector')
    values = residuals.detach().double().tolist()
    with native_factory(graph) as native:
        search = native.search(values, baseline=False)
    paths, checks = adapter.adapt_candidate_paths(graph, values, search)
    loss, details = ranking.combined_condition_loss(graph, target, residuals,
                        search, prepared['valid'], paths, objective)
    rank = details['ranking']
    safe = {'status': details['status'], 'mode': mode,
            'context_token_count': len(context_tokens), 'edge_count': len(values),
            'candidate_path_count': len(paths), 'adapter_nodes_checked': checks['nodes_checked'],
            'ranking_status': rank['status'], 'ranking_eligible': rank['ranking_eligible'],
            'ranking_applied': details['ranking_applied'],
            'ranking_term': details['ranking_term'],
            'same_target_text_path_count': rank['same_target_text_paths'],
            'different_text_negative_path_count': rank['negative_paths'],
            'combined_loss': None if loss is None else float(loss.detach()),
            'forward_calls': 1, 'native_calls': 1, 'optimizer_updates': 0}
    return loss, safe


def scene_condition_mean(scene_losses):
    """Original condition mean per scene, then unweighted scene mean; no skipping."""
    import torch
    need(type(scene_losses) in (list, tuple) and bool(scene_losses), 'nonempty scenes')
    means = []
    for conditions in scene_losses:
        need(type(conditions) in (list, tuple) and bool(conditions), 'nonempty scene conditions')
        for loss in conditions:
            need(isinstance(loss, torch.Tensor) and loss.ndim == 0 and
                 bool(torch.isfinite(loss)), 'every declared condition has finite scalar loss')
        means.append(torch.stack(list(conditions)).mean())
    return torch.stack(means).mean()
