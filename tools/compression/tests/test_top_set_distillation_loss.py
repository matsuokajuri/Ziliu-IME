"""Synthetic tensors only; no models, datasets, teacher calls or checkpoints."""
import math
import unittest
import torch
from tiny_sequence_scorer import compression_loss
from top_set_distillation_loss import comparison_loss, top_set_pairwise_loss

class Tests(unittest.TestCase):
    def test_expected_value_and_gradient_direction(self):
        s = torch.tensor([0., 1., -1.], dtype=torch.float64, requires_grad=True)
        t = torch.tensor([2., 0., -1.], dtype=torch.float64, requires_grad=True)
        loss = top_set_pairwise_loss(s, t)
        self.assertAlmostEqual(loss.item(), (math.log1p(math.exp(1)) + math.log1p(math.exp(-1))) / 2, places=14)
        loss.backward()
        self.assertTrue(torch.isfinite(s.grad).all())
        self.assertLess(s.grad[0].item(), 0)
        self.assertTrue((s.grad[1:] > 0).all())
        self.assertIsNone(t.grad)
        self.assertAlmostEqual(s.grad.sum().item(), 0, places=14)

    def test_top_ties_no_forced_internal_order(self):
        s = torch.tensor([0., 0., 0.], dtype=torch.float64, requires_grad=True)
        t = torch.tensor([2., 2. - 0.5e-6, 0.], dtype=torch.float64)
        loss = top_set_pairwise_loss(s, t)
        self.assertAlmostEqual(loss.item(), math.log(2), places=14)
        loss.backward()
        torch.testing.assert_close(s.grad, torch.tensor([-.25, -.25, .5], dtype=torch.float64))

    def test_all_ties_and_singleton_differentiable_zero(self):
        for t in ([1.], [1., 1.], [1., 1. - 0.5e-6]):
            s = torch.arange(len(t), dtype=torch.float64, requires_grad=True)
            loss = top_set_pairwise_loss(s, torch.tensor(t, dtype=torch.float64))
            self.assertEqual(loss.item(), 0)
            loss.backward()
            torch.testing.assert_close(s.grad, torch.zeros_like(s))

    def test_teacher_dtype_membership_not_downcast(self):
        s = torch.zeros(3, dtype=torch.float32, requires_grad=True)
        t = torch.tensor([100., 100. - 2e-6, 99.], dtype=torch.float64)
        top_set_pairwise_loss(s, t).backward()
        torch.testing.assert_close(s.grad, torch.tensor([-.5, .25, .25]))

    def test_shift_invariance(self):
        s = torch.tensor([-.5, .25, 1.], dtype=torch.float64, requires_grad=True)
        t = torch.tensor([2., 2., -1.], dtype=torch.float64)
        for fn in (top_set_pairwise_loss, lambda a,b: comparison_loss(a,b,arm='B')):
            base = fn(s, t); shifted = fn(s + 8., t - 16.)
            torch.testing.assert_close(base, shifted, rtol=1e-12, atol=1e-12)
            torch.testing.assert_close(torch.autograd.grad(base, s)[0], torch.autograd.grad(shifted, s)[0], rtol=1e-12, atol=1e-12)

    def test_permutation_invariance_and_gradient_equivariance(self):
        s = torch.tensor([.25, -.5, 1., 0.], dtype=torch.float64, requires_grad=True)
        t = torch.tensor([2., 2., -1., 0.], dtype=torch.float64)
        perm = torch.tensor([3, 1, 0, 2])
        for fn in (top_set_pairwise_loss, lambda a,b: comparison_loss(a,b,arm='B')):
            a = fn(s, t); sp = s.detach()[perm].requires_grad_(); b = fn(sp, t[perm])
            torch.testing.assert_close(a, b, rtol=1e-12, atol=1e-12)
            torch.testing.assert_close(torch.autograd.grad(a,s)[0][perm], torch.autograd.grad(b,sp)[0], rtol=1e-12, atol=1e-12)

    def test_arms_exact_definition(self):
        s = torch.tensor([0., 1., -.5], requires_grad=True); t = torch.tensor([1., -1., .25], requires_grad=True)
        a = comparison_loss(s,t,arm='A'); b = comparison_loss(s,t,arm='B')
        self.assertTrue(torch.equal(a, compression_loss(s,t)))
        self.assertTrue(torch.equal(b, a + .1 * top_set_pairwise_loss(s,t)))
        b.backward(); self.assertTrue(torch.isfinite(s.grad).all()); self.assertIsNone(t.grad)

    def test_positive_and_negative_difference_overflow_rejected(self):
        for values in ([3e38, -3e38], [-3e38, 3e38]):
            with self.assertRaisesRegex(ValueError, 'difference overflow'):
                top_set_pairwise_loss(torch.tensor(values, dtype=torch.float32), torch.tensor([1., 0.]))

    def test_nonfinite_baseline_rejected_for_both_arms(self):
        for arm in ('A', 'B'):
            with self.assertRaisesRegex(ValueError, 'baseline loss overflow'):
                comparison_loss(torch.tensor([1e20, -1e20]), torch.tensor([1., 0.]), arm=arm)

    def test_bad_inputs_rejected(self):
        for s,t in (([], []), ([1.]*10,[1.]*10), ([float('nan')],[0.]), ([0.],[float('inf')]), ([1.,2.],[1.])):
            with self.assertRaises(ValueError):top_set_pairwise_loss(torch.tensor(s),torch.tensor(t))
        with self.assertRaises(ValueError):top_set_pairwise_loss(torch.tensor([1]),torch.tensor([1]))
        with self.assertRaises(ValueError):comparison_loss(torch.tensor([1.]),torch.tensor([1.]),arm='C')

if __name__ == '__main__': unittest.main()
