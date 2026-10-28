"""Random synthetic embedding and token IDs only; no real weights or DEV input."""
import unittest
from unittest.mock import patch
import torch
from tiny_sequence_scorer import TinySequenceScorer
from full_pool_scorer import score_full_pool

class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        generator = torch.Generator().manual_seed(17)
        cls.model = TinySequenceScorer(torch.randn(8192,192,generator=generator),seed=23).eval()
        cls.prefix = [2,7,11]
        cls.pool = [[3+i, 200+i, 400+(i%13)] for i in range(134)]

    def test_sizes_counts_and_single_candidate_bitwise_equivalence(self):
        with torch.no_grad():
            singles = torch.cat([self.model(self.prefix,[tail]) for tail in self.pool])
        for n in (1,9,10,67,134):
            calls=[];forward=self.model.forward
            def traced(prefix,candidates):
                calls.append((len(candidates),torch.is_grad_enabled()))
                return forward(prefix,candidates)
            with patch.object(self.model,'forward',side_effect=traced):
                result=score_full_pool(self.model,self.prefix,self.pool[:n])
            self.assertTrue(torch.equal(result,singles[:n]))
            self.assertEqual(len(calls),(n+8)//9)
            self.assertEqual([size for size,grad in calls],[min(9,n-i) for i in range(0,n,9)])
            self.assertTrue(all(not grad for size,grad in calls));self.assertFalse(result.requires_grad)

    def test_permutation_equivariance_bitwise(self):
        original=score_full_pool(self.model,self.prefix,self.pool)
        order=torch.randperm(134,generator=torch.Generator().manual_seed(47)).tolist()
        permuted=score_full_pool(self.model,self.prefix,[self.pool[i] for i in order])
        self.assertTrue(torch.equal(permuted,original[order]))

    def test_length_64_boundary(self):
        prefix=[2];tail=[3]*63
        result=score_full_pool(self.model,prefix,[tail])
        with torch.no_grad():expected=self.model(prefix,[tail])
        self.assertTrue(torch.equal(result,expected))

    def test_illegal_pool_validated_before_any_forward(self):
        cases=[([],[[3]]),([2],[]),([2],[[3]]*135),([2],[[]]),([2],[[8192]]),([2],[[2]]),([2],[[True]]),([2,8192],[[3]]),([2],[[3]*64]),([2],[[3]]*9+[[8192]])]
        for prefix,pool in cases:
            with patch.object(self.model,'forward') as forward:
                with self.assertRaises(ValueError):score_full_pool(self.model,prefix,pool)
                forward.assert_not_called()

    def test_explicit_eval_required(self):
        self.model.train()
        try:
            with self.assertRaises(ValueError):score_full_pool(self.model,self.prefix,self.pool[:1])
        finally:self.model.eval()
        self.model.hidden.train()
        try:
            with self.assertRaises(ValueError):score_full_pool(self.model,self.prefix,self.pool[:1])
        finally:self.model.eval()

    def test_inputs_parameters_and_modes_unchanged(self):
        prefix=self.prefix[:];pool=[tail[:] for tail in self.pool[:10]]
        before={name:p.detach().clone() for name,p in self.model.named_parameters()}
        score_full_pool(self.model,prefix,pool)
        self.assertEqual(prefix,self.prefix);self.assertEqual(pool,self.pool[:10])
        self.assertTrue(all(torch.equal(p,before[name]) for name,p in self.model.named_parameters()))
        self.assertTrue(all(p.grad is None for p in self.model.parameters()))
        self.assertTrue(all(not module.training for module in self.model.modules()))

    def test_bad_output_rejected(self):
        for output in (torch.tensor([float('nan')]),torch.zeros(2),torch.zeros(1,dtype=torch.float64)):
            with patch.object(self.model,'forward',return_value=output):
                with self.assertRaises(ValueError):score_full_pool(self.model,self.prefix,self.pool[:1])

if __name__=='__main__':unittest.main()
