"""Invented-only tensor tests; explicit trusted original objective input."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def fixture():
    # ab gold, ac strongest wrong, ad weaker; shared first edge must cancel.
    p = ORIGINAL.PENALTY
    graph = {'total': 2, 'entries': [{'text': t, 'weight': -p + w}
              for t, w in [('a', 0), ('b', 0), ('c', 2), ('d', 1), ('b', -1)]],
             'edges': [(0, 1, 0), (1, 2, 1), (1, 2, 2), (1, 2, 3), (1, 2, 4)]}
    search = {'nodes': [(10, 1, 0, 0, 0, 0.0)], 'states': [(1, [10])],
              'pool': [{'text': 'ac'}, {'text': 'ab'}]}
    return graph, search


class Tests(unittest.TestCase):
    def setUp(self):
        self.g, self.search = fixture()
        self.r = torch.zeros(5, requires_grad=True)
        self.paths = [(0, 1), (0, 2), (0, 3)]

    def loss(self, paths=None, target='ab', graph=None, residuals=None):
        return RANK.ranking_loss(self.g if graph is None else graph, target,
            self.r if residuals is None else residuals,
            self.paths if paths is None else paths, ORIGINAL)

    def test_strongest_negative_and_shared_gradient(self):
        loss, info = self.loss()
        self.assertEqual(info['negative_path'], [0, 2])
        self.assertEqual(loss.item(), 3.0)
        loss.backward()
        self.assertEqual(self.r.grad.tolist(), [0.0, -1.0, 1.0, 0.0, 0.0])

    def test_marginranking_reference(self):
        loss, info = self.loss()
        pos = ORIGINAL.differentiable_score(self.g, (0, 1), self.r)
        neg = ORIGINAL.differentiable_score(self.g, (0, 2), self.r)
        ref = torch.nn.functional.margin_ranking_loss(pos.reshape(1), neg.reshape(1),
                torch.ones(1, dtype=pos.dtype), margin=1.0)
        self.assertEqual(loss.item(), ref.item())

    def test_same_text_alternative_is_not_negative(self):
        loss, info = self.loss([(0, 1), (0, 4)])
        self.assertIsNone(loss)
        self.assertEqual(info['status'], 'NO_DIFFERENT_TEXT_NEGATIVE')
        self.assertEqual(info['same_target_text_paths'], 2)

    def test_no_negative_preserves_nonzero_original(self):
        residuals = torch.ones(5, requires_grad=True)
        valid = torch.ones(5, dtype=torch.bool)
        old, _ = ORIGINAL.condition_loss(self.g, 'ab', residuals, self.search, valid)
        self.assertGreater(old.item(), 0)
        combined, info = RANK.combined_condition_loss(self.g, 'ab', residuals,
              self.search, valid, [(0, 1), (0, 4)], ORIGINAL)
        self.assertEqual(combined.item(), old.item())
        self.assertFalse(info['ranking_applied'])
        self.assertIsNone(info['ranking_term'])

    def test_unreachable_is_explicit(self):
        loss, info = self.loss(target='zz')
        self.assertIsNone(loss)
        self.assertEqual(info['status'], 'UNREACHABLE_TARGET')
        combined, details = RANK.combined_condition_loss(self.g, 'zz', self.r,
             self.search, torch.ones(5, dtype=torch.bool), self.paths, ORIGINAL)
        self.assertIsNone(combined)
        self.assertFalse(details['ranking_applied'])

    def test_bad_paths_rejected(self):
        for path in [(), (1,), (0,), (0, 0), (0, 7), (True, 1), (-1, 1)]:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.loss([path])

    def test_nonfinite_rejected(self):
        for value in [float('nan'), float('inf'), float('-inf')]:
            g = copy.deepcopy(self.g); g['entries'][0]['weight'] = value
            with self.assertRaises(ValueError): self.loss(graph=g)
            r = self.r.detach().clone(); r[0] = value
            with self.assertRaises(ValueError): self.loss(residuals=r)

    def test_combined_retains_original_plus_rank(self):
        valid = torch.ones(5, dtype=torch.bool)
        original, _ = ORIGINAL.condition_loss(self.g, 'ab', self.r, self.search, valid)
        rank, _ = self.loss()
        combined, info = RANK.combined_condition_loss(self.g, 'ab', self.r,
                    self.search, valid, self.paths, ORIGINAL)
        self.assertEqual(combined.item(), original.item() + rank.item())
        self.assertTrue(info['ranking_applied'])

    def test_satisfied_margin_zero_is_eligible(self):
        r = self.r.detach().clone(); r[1] = 5.0; r.requires_grad_()
        loss, info = self.loss(residuals=r)
        self.assertEqual(loss.item(), 0.0)
        self.assertTrue(info['ranking_eligible'])

    def test_tie_is_deterministic(self):
        g = copy.deepcopy(self.g); g['entries'][3]['weight'] = g['entries'][2]['weight']
        loss, info = self.loss(paths=[(0, 3), (0, 2)], graph=g)
        self.assertEqual(info['negative_path'], [0, 2])

    def test_higher_same_text_gold_path_selected(self):
        g = copy.deepcopy(self.g); g['entries'][4]['weight'] = -ORIGINAL.PENALTY + 4
        loss, info = self.loss(graph=g)
        self.assertEqual(info['gold_path'], [0, 4])
        self.assertEqual(loss.item(), 0.0)

    def test_exact_margin_has_zero_loss_and_gradient(self):
        r = self.r.detach().clone(); r[1] = 3.0; r.requires_grad_()
        loss, info = self.loss(residuals=r)
        self.assertEqual(loss.item(), 0.0)
        loss.backward()
        self.assertEqual(r.grad.tolist(), [0.0] * 5)

    def test_no_negative_preserves_original_gradient(self):
        r1 = torch.ones(5, requires_grad=True)
        r2 = torch.ones(5, requires_grad=True)
        valid = torch.ones(5, dtype=torch.bool)
        old, _ = ORIGINAL.condition_loss(self.g, 'ab', r1, self.search, valid)
        combined, info = RANK.combined_condition_loss(self.g, 'ab', r2,
            self.search, valid, [(0, 1), (0, 4)], ORIGINAL)
        old.backward(); combined.backward()
        self.assertTrue(torch.equal(r1.grad, r2.grad))
        self.assertGreater(r2.grad.abs().sum().item(), 0)

    def test_empty_pool_is_no_negative_not_success(self):
        loss, info = self.loss([])
        self.assertIsNone(loss)
        self.assertFalse(info['ranking_eligible'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--objective', required=True)
    args = parser.parse_args()
    path = Path(args.objective).resolve(strict=True)
    sys.modules['data'] = module('data', path.with_name('data.py'))
    ORIGINAL = module('original_objective', path)
    RANK = module('rank_objective', Path(__file__).with_name('rank_objective.py'))
    import torch
    torch.set_num_threads(2); torch.set_num_interop_threads(1)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    print(json.dumps({'tests': result.testsRun, 'failures': len(result.failures),
          'errors': len(result.errors), 'synthetic_only': True, 'training_updates': 0,
          'native_execution': False, 'margin': RANK.MARGIN, 'ranking_weight': RANK.RANKING_WEIGHT}))
    sys.exit(0 if result.wasSuccessful() else 1)
