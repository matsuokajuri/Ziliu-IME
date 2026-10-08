"""Deferred tiny synthetic tensor checks. NOT executed in the code-only handoff.

Run only inside an independently authorized owned Job with an audited local PyTorch.
Environment flag asserts that external precondition; it neither creates nor grants it.
"""
import os
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tests"))
from test_ranking_student_stdlib import request, row
from tools.offline.ranking_student.codec import Codec
from tools.offline.ranking_student.config import tiny_config
from tools.offline.ranking_student.model import build_model, train_step
from tools.offline.ranking_student.contracts import digest
from tools.offline.ranking_student.reference import objective_and_gradient


@unittest.skipUnless(os.environ.get("ZILIU_STUDENT_RUNTIME_WINDOW") == "AUTHORIZED_OWNED_JOB",
                     "deferred: no authorized PyTorch resource window")
class RuntimeTests(unittest.TestCase):
    def setUp(self):
        import torch
        self.torch = torch
        torch.set_num_threads(1)
        torch.manual_seed(7)
        self.config = tiny_config()
        self.codec = Codec(config=self.config)
        self.model = build_model(self.config,runtime_authorized=True).eval()

    def test_shape_count_and_gradient(self):
        r = row(); plan = self.codec.plan([r["request"]])
        scores = self.model(**self.model.tensors(plan))
        self.assertEqual(tuple(scores.shape),(1,2))
        self.assertEqual(sum(p.numel() for p in self.model.parameters()),self.config.parameter_count())
        loss = self.model.objective(scores,plan,[r]); loss.backward()
        for name,p in self.model.named_parameters():
            self.assertIsNotNone(p.grad,name)
            self.assertTrue(self.torch.isfinite(p.grad).all(),name)
        for name in ("cross_q.weight","cross_k.weight","cross_v.weight","score_head.0.weight","token.weight"):
            self.assertGreater(float(dict(self.model.named_parameters())[name].grad.abs().sum()),0,name)

    def test_permutation_and_padding_invariance(self):
        a = request(); b = request(); b["candidates"].reverse()
        single = request(); single["candidates"] = single["candidates"][:1]
        with self.torch.no_grad():
            sa = self.model(**self.model.tensors(self.codec.plan([a])))
            sb = self.model(**self.model.tensors(self.codec.plan([b])))
            batch = self.model(**self.model.tensors(self.codec.plan([single,a])))
            one = self.model(**self.model.tensors(self.codec.plan([single])))
        self.torch.testing.assert_close(sa,sb.flip(-1),atol=1e-5,rtol=1e-5)
        self.torch.testing.assert_close(batch[0,:1],one[0],atol=1e-5,rtol=1e-5)
        self.assertTrue(self.torch.isneginf(batch[0,1]))

    def test_source_and_candidate_length_padding_invariance(self):
        a = request()
        original_plan = self.codec.plan([a])
        with self.torch.no_grad():
            original = self.model(**self.model.tensors(original_plan))
            for grow_source,grow_candidate in ((True,False),(False,True),(True,True)):
                b = copy.deepcopy(a)
                if grow_source: b["prefix"] = "合成测试"*12
                if grow_candidate: b["candidates"][1]["text"] = "更长的合成候选"*7
                plan = self.codec.plan([a,b])
                with self.subTest(source_padding=grow_source,candidate_padding=grow_candidate):
                    if grow_source:
                        self.assertGreater(len(plan["source_ids"][0]),len(original_plan["source_ids"][0]))
                    if grow_candidate:
                        self.assertGreater(len(plan["candidate_ids"][0][0]),len(original_plan["candidate_ids"][0][0]))
                    padded = self.model(**self.model.tensors(plan))
                    self.torch.testing.assert_close(original[0],padded[0],atol=1e-5,rtol=1e-5)
                    swapped = self.model(**self.model.tensors(self.codec.plan([b,a])))
                    self.torch.testing.assert_close(original[0],swapped[1],atol=1e-5,rtol=1e-5)

    def test_unrelated_candidate_perturbation_isolation(self):
        a = request()
        plan = self.codec.plan([a])
        with self.torch.no_grad():
            original = self.model(**self.model.tensors(plan))
            for other_text in ("另一合成候选","更长的合成候选"*7):
                changed = copy.deepcopy(a)
                changed["candidates"][1]["text"] = other_text
                changed_plan = self.codec.plan([changed])
                self.assertEqual(changed_plan["source_ids"],plan["source_ids"])
                self.assertNotEqual(changed_plan["candidate_ids"][0][1],plan["candidate_ids"][0][1])
                perturbed = self.model(**self.model.tensors(changed_plan))
                self.torch.testing.assert_close(original[0,0],perturbed[0,0],atol=1e-5,rtol=1e-5)

    def test_tensor_loss_and_gradient_match_stdlib(self):
        for kind,target in (("teacher_distribution",[.25,.75]),("acceptable_set",[1])):
            r = row()
            if kind == "acceptable_set":
                r["target"].update(kind=kind,probabilities=[],teachers=[],acceptable_source_indices=[2])
            plan = self.codec.plan([r["request"]])
            scores = self.torch.tensor([[.2,-.3]],dtype=self.torch.float64,requires_grad=True)
            loss = self.model.objective(scores,plan,[r]); loss.backward()
            expected,grad = objective_and_gradient([.2,-.3],target,kind=kind)
            self.assertAlmostEqual(float(loss.detach()),expected,places=12)
            for actual,want in zip(scores.grad[0].tolist(),grad): self.assertAlmostEqual(actual,want,places=12)

    def test_shared_context_projection_once(self):
        counts = {"context":0,"key":0,"value":0}
        handles = []
        for name,module in (("context",self.model.context_blocks[0]),("key",self.model.cross_k),("value",self.model.cross_v)):
            def hook(module,args,output,name=name): counts[name] += 1
            handles.append(module.register_forward_hook(hook))
        self.model(**self.model.tensors(self.codec.plan([request()])))
        for handle in handles: handle.remove()
        self.assertEqual(counts,{"context":1,"key":1,"value":1})

    def test_complete_request_and_tokenizer_binding(self):
        r = row(); plan = self.codec.plan([r["request"]])
        scores = self.model(**self.model.tensors(plan))
        r["request"]["prefix"] = "另一合成上下文"; r["pool_sha256"] = digest(r["request"])
        with self.assertRaises(ValueError): self.model.objective(scores,plan,[r])
        changed = Codec("甲乙",config=self.config)
        with self.assertRaises(ValueError): self.model.tensors(changed.plan([request()]))

    def test_tiny_teacher_probabilities_stay_finite(self):
        r = row(); r["request"]["candidates"].append({"source_index":3,"text":"合成第三项"})
        r["pool_sha256"] = digest(r["request"])
        r["target"].update(candidate_source_indices=[0,2,3],probabilities=[1.,1e-300,1e-300])
        plan = self.codec.plan([r["request"]])
        scores = self.torch.tensor([[.1,.2,.3]],requires_grad=True)
        loss = self.model.objective(scores,plan,[r]); self.model.backward_checked(loss)
        self.assertTrue(self.torch.isfinite(scores.grad).all())

    def test_nonfinite_loss_refuses_optimizer_update(self):
        optimizer = Mock()
        self.model.objective = lambda *args: self.torch.tensor(float("nan"),requires_grad=True)
        with self.assertRaises(ValueError):
            train_step(self.model,optimizer,[row()],self.codec,runtime_authorized=True)
        optimizer.step.assert_not_called()

    def test_all_masked_rejected_and_padding_zero_gradient(self):
        short = row(); short["request"]["candidates"] = short["request"]["candidates"][:1]
        short["pool_sha256"] = digest(short["request"])
        short["target"].update(candidate_source_indices=[0],probabilities=[1.])
        plan = self.codec.plan([short["request"],request()])
        tensors = self.model.tensors(plan)
        bad = dict(tensors); bad["source_mask"] = self.torch.zeros_like(tensors["source_mask"])
        with self.assertRaises(ValueError): self.model(**bad)
        scores = self.torch.tensor([[.1,float("-inf")],[.2,.3]],requires_grad=True)
        self.model.objective(scores,plan,[short,row()]).backward()
        self.assertEqual(float(scores.grad[0,1]),0.)


if __name__ == "__main__": unittest.main()
