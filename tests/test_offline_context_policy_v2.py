"""Separate evidence/protection and enforce research-only conditional gates."""
from pathlib import Path
import sys
import unittest
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "scripts"))
import offline_context_policy_v2 as P


class PolicyTests(unittest.TestCase):
    def candidate(self, kind="phrase", dynamic="Phrase", trusted=True):
        return {"text":"fixture", "answers_key":True, "trusted_dictionary_hit":trusted,
            "native_genuines":[{"text":"fixture", "dynamic_type":dynamic, "type":kind,
                                "canonical_tokens":["x","y"]}]}

    def evidence(self, **kwargs):
        return P.source_evidence(self.candidate(),"xy",isolated_schema_qualified=True,**kwargs)

    def gate(self, evidence=None, **kwargs):
        return P.research_gate("synthetic prefix",["fixture","other"],evidence or [self.evidence()]*2,**kwargs)

    def test_general_system_hit_is_evidence_and_not_explicit_fixed_preference(self):
        fact=self.evidence()
        self.assertEqual(fact.origin,"ordinary_system_exact")
        self.assertFalse(fact.strong_protection)
        self.assertEqual(self.gate().indices,(0,1))

    def test_receipts_protect_even_nonleader_and_never_authenticate_production(self):
        for field in ("explicit_choice_receipt","dedicated_phrase_receipt"):
            evidence=[self.evidence(),self.evidence(**{field:True})]
            self.assertFalse(self.gate(evidence).indices)
        self.assertEqual(self.gate(production_requested=True).reason,"production_not_admitted")

    def test_user_learning_table_and_ambiguous_sources_remain_guarded(self):
        for kind in ("user_phrase","user_table","completion"):
            fact=P.source_evidence(self.candidate(kind),"xy",isolated_schema_qualified=True)
            self.assertFalse(self.gate([fact,self.evidence()]).indices)
        candidate=self.candidate()
        candidate["native_genuines"]*=2
        fact=P.source_evidence(candidate,"xy",isolated_schema_qualified=True)
        self.assertEqual(fact.origin,"unknown")

    def test_literal_spelling_and_whole_key_proof_cannot_be_replaced_by_text_length(self):
        fact=P.source_evidence(self.candidate(),"xx",isolated_schema_qualified=True)
        self.assertEqual(fact.origin,"unknown")
        for whole in (None,False):
            fact=P.SourceEvidence("ordinary_system_exact",whole,True,False)
            self.assertFalse(self.gate([fact,self.evidence()]).indices)

    def test_missing_stale_forbidden_prefix_declines(self):
        self.assertFalse(P.research_gate("",["x","y"],[self.evidence()]*2).indices)
        self.assertFalse(self.gate(current_prefix=False).indices)
        self.assertFalse(self.gate(ordinary_scope=False).indices)

    def test_joint_evidence_and_margin_are_required(self):
        threshold=P.Thresholds(.25,.25,.1)
        gate=self.gate()
        self.assertEqual(P.decide(gate,[-2,-1],[-1,0],threshold).promotion,1)
        self.assertIsNone(P.decide(gate,[-2,-1],[0,-1],threshold).promotion)
        self.assertIsNone(P.decide(gate,[-2,-1.8],[-1,0],threshold).promotion)
        self.assertIsNone(P.decide(gate,[-1,-1],[0,0],P.Thresholds(0,0,0)).promotion)

    def test_unknown_boolean_and_nonfinite_score_are_not_coerced(self):
        candidate=self.candidate()
        candidate["answers_key"]=1
        with self.assertRaises(ValueError):
            P.source_evidence(candidate,"xy",isolated_schema_qualified=True)
        with self.assertRaises(ValueError):
            P.decide(self.gate(),[-2,float("nan")],[0,1],P.Thresholds(0,0,0))
        for values in ((-1,0,0),(float("inf"),0,0),(True,0,0)):
            with self.assertRaises(ValueError):
                P.Thresholds(*values)
        with self.assertRaises(ValueError):
            P.SourceEvidence("ordinary_system_exact",1,True,False)
        for indices in ((0,99),(0,False),(0,2,1)):
            with self.assertRaises(ValueError):
                P.decide(P.Decision(indices,None,"synthetic"),[0]*len(indices),[0]*len(indices),P.Thresholds(0,0,0))

    def test_candidate_containers_and_evidence_objects_cannot_be_coerced(self):
        valid=[self.evidence(),self.evidence()]
        for texts,evidence in (("xy",valid),({0:"x",1:"y"},valid),
                (["x","y"],iter(valid)),(["x","y"],[None,self.evidence()]),
                (["x","y"],[{"origin":"ordinary_system_exact"},self.evidence()])):
            with self.assertRaises(ValueError):
                P.research_gate("synthetic prefix",texts,evidence)

    def test_oversized_prefix_declines_before_unicode_encoding(self):
        with mock.patch.object(P,"validate_text",side_effect=RuntimeError("must not encode")):
            with self.assertRaisesRegex(ValueError,"bounded research prefix"):
                P.research_gate(
                    "x"*16_385,["x","y"],[self.evidence(),self.evidence()])


if __name__=="__main__":
    unittest.main()
