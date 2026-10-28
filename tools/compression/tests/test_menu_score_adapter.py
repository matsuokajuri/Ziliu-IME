"""Standard-library-only synthetic coverage; no real examples or model calls."""
import math
import sys
import unittest
from menu_score_adapter import shift_scores_for_menu

class Tests(unittest.TestCase):
    def test_negative_positive_mixed(self):
        for values, expected in (([-3.,-1.,-2.],[-2.,0.,-1.]),([3.,1.,2.],[0.,-2.,-1.]),([-1.,2.,0.],[-3.,0.,-2.])):
            self.assertEqual(shift_scores_for_menu(values),expected)

    def test_ties_and_singleton(self):
        self.assertEqual(shift_scores_for_menu([2.,2.,1.,1.]),[0.,0.,-1.,-1.])
        self.assertEqual(shift_scores_for_menu([0.,-0.]),[0.,0.])
        self.assertEqual(shift_scores_for_menu([9.]),[0.])
        self.assertEqual(shift_scores_for_menu([5.]*134),[0.]*134)

    def test_shift_and_permutation(self):
        values=[-4.,2.,1.,2.];translated=shift_scores_for_menu(values)
        self.assertEqual(translated,shift_scores_for_menu([x+8 for x in values]))
        order=[3,0,2,1]
        self.assertEqual([translated[i] for i in order],shift_scores_for_menu([values[i] for i in order]))

    def test_complete_134_list_pairwise_preserved(self):
        values=[float((i*53)%67) for i in range(134)];out=shift_scores_for_menu(values)
        self.assertEqual(len(out),134)
        for i in range(134):
            for j in range(i):
                self.assertEqual((values[i]>values[j],values[i]==values[j]),(out[i]>out[j],out[i]==out[j]))

    def test_raw_not_modified_or_aliased(self):
        values=[2.,1.];copy=values[:];out=shift_scores_for_menu(values)
        self.assertEqual(values,copy);self.assertIsNot(out,values)

    def test_overflow_rejected(self):
        with self.assertRaisesRegex(ValueError,'non-finite'):shift_scores_for_menu([sys.float_info.max,-sys.float_info.max])
        with self.assertRaises(ValueError):shift_scores_for_menu([10**1000])

    def test_translation_precision_tie_collapse_rejected(self):
        with self.assertRaisesRegex(ValueError,'translation changes'):shift_scores_for_menu([1e308,1.,2.])

    def test_conversion_precision_tie_collapse_rejected(self):
        with self.assertRaisesRegex(ValueError,'conversion changes'):shift_scores_for_menu([2**53,2**53+1])

    def test_invalid_inputs(self):
        for values in ([],[1.]*135,[math.nan],[math.inf],[-math.inf],[True],['1'],[1+0j]):
            with self.assertRaises(ValueError):shift_scores_for_menu(values)

if __name__=='__main__':unittest.main()
