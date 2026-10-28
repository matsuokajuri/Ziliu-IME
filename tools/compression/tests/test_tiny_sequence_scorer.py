import unittest
import torch
from tiny_sequence_scorer import TinySequenceScorer, compression_loss


class StudentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.manual_seed(7)
        cls.embedding = torch.randn(8192, 192)

    def test_geometry_frozen_dependency_and_no_rng_side_effect(self):
        rng = torch.get_rng_state().clone()
        model = TinySequenceScorer(self.embedding)
        self.assertTrue(torch.equal(rng, torch.get_rng_state()))
        self.assertEqual(sum(p.numel() for p in model.parameters()), 23041)
        self.assertFalse(model.embedding.requires_grad)
        self.assertNotIn('embedding', model.state_dict())
        self.assertNotEqual(model.embedding.data_ptr(), self.embedding.data_ptr())

    def test_candidate_order_is_equivariant(self):
        model = TinySequenceScorer(self.embedding)
        a = model([2, 8], [[3, 4], [5], [6, 7]])
        b = model([2, 8], [[6, 7], [3, 4], [5]])
        self.assertTrue(torch.equal(a[[2, 0, 1]], b))

    def test_gradients_finite_and_teacher_detached(self):
        model = TinySequenceScorer(self.embedding)
        teacher = torch.tensor([-2.0, -3.0], requires_grad=True)
        loss = compression_loss(model([2, 8], [[3, 4], [5, 6]]), teacher)
        loss.backward()
        self.assertIsNone(teacher.grad)
        self.assertIsNone(model.embedding.grad)
        self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all()
                            for p in model.parameters()))

    def test_loss_invariant_to_additive_score_offsets(self):
        s = torch.tensor([0.3, -0.2, 0.8], dtype=torch.float64)
        t = torch.tensor([-2.1, -4.2, -3.3], dtype=torch.float64)
        self.assertTrue(torch.allclose(compression_loss(s, t),
                                      compression_loss(s + 12, t - 4), atol=1e-12, rtol=0))

    def test_matching_scores_have_zero_loss(self):
        x = torch.tensor([-2.0, -3.0], dtype=torch.float64)
        self.assertLess(abs(compression_loss(x, x).item()), 1e-14)

    def test_contract_rejects_invalid_sequences(self):
        model = TinySequenceScorer(self.embedding)
        cases = [([], [[3]]), ([3], [[4]]), ([2], [[]]),
                 ([2], [[True]]), ([2], [[1]]), ([2], [[8192]]),
                 ([2] * 64, [[3]]), ([2], [[3]] * 10)]
        for prefix, candidates in cases:
            with self.subTest(prefix=prefix, candidates=candidates), self.assertRaises(ValueError):
                model(prefix, candidates)

    def test_loss_rejects_nonfinite_or_shape_mismatch(self):
        for teacher in (torch.tensor([float('nan'), 0]), torch.tensor([1.0])):
            with self.assertRaises(ValueError):
                compression_loss(torch.tensor([0.0, 1.0]), teacher)

    def test_weight_roundtrip_same_external_embedding(self):
        a = TinySequenceScorer(self.embedding)
        b = TinySequenceScorer(self.embedding, seed=1)
        b.load_state_dict(a.state_dict(), strict=True)
        self.assertTrue(torch.equal(a([2], [[3, 4], [5]]), b([2], [[3, 4], [5]])))


if __name__ == '__main__':
    unittest.main()
