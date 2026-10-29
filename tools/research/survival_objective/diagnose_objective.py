"""Invented frontier records expose survival-vs-ranking objective scope.

The original objective and adjacent data.py are explicit trusted caller inputs.
No scene dataset, checkpoint, model, vocabulary or native library is opened.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


def load_objective(path):
    path = Path(path).resolve(strict=True)
    data_path = path.with_name('data.py')
    old = sys.modules.get('data')
    spec = importlib.util.spec_from_file_location('data', data_path)
    data = importlib.util.module_from_spec(spec)
    sys.modules['data'] = data
    try:
        spec.loader.exec_module(data)
        spec = importlib.util.spec_from_file_location('diagnosed_objective', path)
        objective = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(objective)
    finally:
        if old is None:
            del sys.modules['data']
        else:
            sys.modules['data'] = old
    return objective, {'objective_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                       'data_sha256': hashlib.sha256(data_path.read_bytes()).hexdigest()}


def fixture(penalty, target_in_pool):
    # Synthetic words ab (gold) and cd (wrong); both use two proper edges.
    graph = {'total': 2, 'edges': [(0, 1, 0), (1, 2, 1), (0, 1, 2), (1, 2, 3)],
             'entries': [{'text': char, 'weight': -penalty + (0 if i < 2 else 1)}
                         for i, char in enumerate('abcd')]}
    # Node layout is the existing objective ABI: id,parent,unused,unused,edge,score.
    nodes = [(10, 1, 0, 0, 2, 1.0)]
    nodes.extend((i, 10, 0, 0, 3, 2.0) for i in range(11, 35))
    pool = [{'text': 'cd'}] + ([{'text': 'ab'}] if target_in_pool else [])
    return graph, {'nodes': nodes, 'states': [(1, [10]), (2, list(range(11, 35)))], 'pool': pool}


def diagnose(path, tensor=False):
    objective, pins = load_objective(path)
    outcomes = []
    for present in (True, False):
        graph, search = fixture(objective.PENALTY, present)
        selected, counts = objective.selected_boundaries(graph, 'ab', [0.0] * 4, search)
        assert objective.gold_path(graph, 'ab', [0.0] * 4) == (0, 1)
        assert counts['terminal_pool_rescued'] == int(present)
        assert counts['unsaturated'] == 1
        assert len(selected) == (0 if present else 1)
        item = {'target_rank': 2 if present else None,
                'target_in_pool': present, 'selected_survival_comparisons': len(selected),
                'counts': counts}
        if tensor:
            import torch
            residuals = torch.zeros(4, requires_grad=True)
            loss, actual_counts = objective.condition_loss(
                graph, 'ab', residuals, search, torch.ones(4, dtype=torch.bool))
            assert actual_counts == counts and loss is not None
            loss.backward()
            assert abs(loss.item() - (0.0 if present else 3.0)) < 1e-12
            assert residuals.grad is not None and torch.isfinite(residuals.grad).all()
            assert (residuals.grad.abs().sum().item() == 0.0) == present
            item.update(loss=loss.item(), gradient_l1=residuals.grad.abs().sum().item())
        outcomes.append(item)
    return {'schema': 'SYNTHETIC_SURVIVAL_OBJECTIVE_SCOPE_V1', 'source_pins': pins,
            'cases': outcomes, 'tensor_execution': tensor, 'native_execution': False,
            'private_data_or_checkpoint_read': False, 'optimizer_updates': 0,
            'conclusion': 'Pool survival does not imply Top1; this diagnoses existing objective scope, not a production defect or a quality result.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--objective', required=True)
    parser.add_argument('--tensor', action='store_true')
    args = parser.parse_args()
    if args.tensor:
        import torch
        torch.set_num_threads(2)
        torch.set_num_interop_threads(1)
    print(json.dumps(diagnose(args.objective, args.tensor), indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
