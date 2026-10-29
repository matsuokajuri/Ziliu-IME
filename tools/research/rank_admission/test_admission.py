"""Synthetic integration only; model and native search are explicit fixture doubles."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import types
import unittest


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec); spec.loader.exec_module(obj)
    return obj


def fixture(residuals):
    graph = {'total': 3, 'entries': [{'text': t, 'weight': w}
             for t, w in [('A', 14.), ('B', 15.), ('C', 16.)]],
             'edges': [[0, 1, 0], [1, 3, 1], [1, 3, 2]]}
    b = [entry['weight'] + ADAPTER.PENALTY for entry in graph['entries']]
    nodes = [[2, 1, 0, 1, 0, b[0] + residuals[0], b[0]],
             [3, 2, 1, 3, 1, b[0] + residuals[0] + b[1] + residuals[1], b[0] + b[1]],
             [4, 2, 1, 3, 2, b[0] + residuals[0] + b[2] + residuals[2], b[0] + b[2]]]
    pool = [dict(node=n, text=t, start=0, end=3, weight=nodes[n-2][6],
            retained_weight=nodes[n-2][6], word_lengths=[1, 2]) for n, t in [(3, 'AB'), (4, 'AC')]]
    snapshot = dict(nodes=nodes, attempts=3, pool=pool, pool_ids=[3, 4],
             states=[[0, [1]], [1, [2]], [3, [3, 4]]], best=[3, 4])
    return graph, snapshot


class Tests(unittest.TestCase):
    def setUp(self):
        self.events = []; self.residuals = torch.tensor([.25, .5, -.75], requires_grad=True)
        self.graph, self.snapshot = fixture(self.residuals.detach().tolist())
        outer = self
        class Model:
            index = {}
            def __call__(self, prepared, context):
                outer.events.append('forward'); return outer.residuals
        class Native:
            def __enter__(self): outer.events.append('native_enter'); return self
            def search(self, values, baseline):
                outer.events.append('search'); assert baseline is False
                assert values == outer.residuals.detach().double().tolist()
                return outer.snapshot
            def __exit__(self, *args): outer.events.append('native_exit')
        def prepare(graph, mode, index, context):
            outer.events.append('prepare'); return {'valid': torch.ones(3, dtype=torch.bool)}
        self.model = Model(); self.native = lambda graph: Native()
        self.packed = types.SimpleNamespace(prepare_graph=prepare)

    def call(self, target='AB'):
        return ADMISSION.condition_loss(self.graph, target, 'pureinitial', [],
            model=self.model, packed=self.packed, native_factory=self.native,
            adapter=ADAPTER, ranking=RANK, objective=ORIGINAL)

    def test_actual_math_wiring(self):
        loss, safe = self.call()
        paths, _ = ADAPTER.adapt_candidate_paths(self.graph, self.residuals.detach().tolist(), self.snapshot)
        expected, _ = RANK.combined_condition_loss(self.graph, 'AB', self.residuals,
                self.snapshot, torch.ones(3, dtype=torch.bool), paths, ORIGINAL)
        self.assertEqual(loss.item(), expected.item())
        self.assertEqual(self.events, ['prepare', 'forward', 'native_enter', 'search', 'native_exit'])
        self.assertEqual(safe['optimizer_updates'], 0)
        self.assertIsNone(self.residuals.grad)

    def test_metadata_excludes_private_fields(self):
        _, safe = self.call(); text = json.dumps(safe)
        self.assertNotIn('AB', text); self.assertNotIn('AC', text)
        for name in ('target', 'paths', 'snapshot', 'context_tokens', 'graph'):
            self.assertNotIn(name, safe)

    def test_no_negative_preserves_original(self):
        self.snapshot['pool'] = self.snapshot['pool'][:1]
        self.snapshot['pool_ids'] = self.snapshot['pool_ids'][:1]
        loss, safe = self.call()
        original, _ = ORIGINAL.condition_loss(self.graph, 'AB', self.residuals,
                     self.snapshot, torch.ones(3, dtype=torch.bool))
        self.assertEqual(loss.item(), original.item())
        self.assertFalse(safe['ranking_applied']); self.assertIsNone(safe['ranking_term'])
        self.assertEqual(safe['ranking_status'], 'NO_DIFFERENT_TEXT_NEGATIVE')

    def test_unreachable_is_not_zero(self):
        loss, safe = self.call('ZZ')
        self.assertIsNone(loss); self.assertIsNone(safe['combined_loss'])
        self.assertEqual(safe['ranking_status'], 'UNREACHABLE_TARGET')

    def test_corrupt_native_snapshot_rejected_after_close(self):
        self.snapshot['nodes'][1][5] += 2
        with self.assertRaises(ValueError): self.call()
        self.assertEqual(self.events[-1], 'native_exit')

    def test_nonfinite_forward_rejected_before_native(self):
        self.residuals = torch.tensor([float('nan'), 0., 0.])
        with self.assertRaises(ValueError): self.call()
        self.assertEqual(self.events, ['prepare', 'forward'])

    def test_two_level_mean_not_pooled(self):
        result = ADMISSION.scene_condition_mean([[torch.tensor(1.), torch.tensor(3.)],
                            [torch.tensor(10.)]])
        self.assertEqual(result.item(), 6.)
        self.assertNotEqual(result.item(), 14. / 3)

    def test_mean_refuses_skip(self):
        for values in [[], [[]], [[None]], [[torch.tensor(float('nan'))]]]:
            with self.assertRaises(ValueError): ADMISSION.scene_condition_mean(values)

    def test_context_rejects_boolean(self):
        with self.assertRaises(ValueError):
            ADMISSION.condition_loss(self.graph, 'AB', 'pureinitial', [True],
              model=self.model, packed=self.packed, native_factory=self.native,
              adapter=ADAPTER, ranking=RANK, objective=ORIGINAL)
        self.assertEqual(self.events, [])

    def test_context_original_bounds(self):
        for tokens in ([0], [2], [8192], [-1], [3] * 65):
            with self.subTest(tokens=tokens), self.assertRaises(ValueError):
                ADMISSION.condition_loss(self.graph, 'AB', 'pureinitial', tokens,
                  model=self.model, packed=self.packed, native_factory=self.native,
                  adapter=ADAPTER, ranking=RANK, objective=ORIGINAL)
        self.assertEqual(self.events, [])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('objective', 'adapter', 'ranking'): parser.add_argument('--' + name, required=True)
    args = parser.parse_args(); original = Path(args.objective).resolve(strict=True)
    sys.modules['data'] = load('data', original.with_name('data.py'))
    ORIGINAL = load('original', original); ADAPTER = load('adapter', args.adapter)
    RANK = load('ranking', args.ranking)
    ADMISSION = load('admission', Path(__file__).with_name('admission.py'))
    import torch
    torch.set_num_threads(2); torch.set_num_interop_threads(1)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    print(json.dumps({'tests': result.testsRun, 'failures': len(result.failures),
          'errors': len(result.errors), 'synthetic_only': True,
          'real_model_calls': 0, 'native_calls': 0, 'optimizer_updates': 0}))
    sys.exit(0 if result.wasSuccessful() else 1)
