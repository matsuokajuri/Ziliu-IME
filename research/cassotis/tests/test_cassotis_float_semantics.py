# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from cassotis_float_reference import controls


class FloatSemantics(unittest.TestCase):
    def test_float_tree_and_cache_match_independent_attention(self):
        result = controls()
        for row in result["fixtures"]:
            self.assertLessEqual(row["float32"]["packed_independent_max_abs_mean_error"], 5e-5)
            self.assertLessEqual(row["float32"]["packed_cached_max_abs_mean_error"], 5e-5)
        self.assertFalse(result["real_model_loaded"])


if __name__ == "__main__":
    unittest.main()
