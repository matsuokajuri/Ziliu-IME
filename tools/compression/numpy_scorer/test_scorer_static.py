"""Stdlib-only checks; importing this suite must not load NumPy."""
import ast
from pathlib import Path
import sys
import unittest
import scorer
import synthetic_qualification

class StaticTests(unittest.TestCase):
    def test_no_numpy_import(self):
        self.assertNotIn('numpy',sys.modules)
    def test_source_imports(self):
        allowed={'hashlib','math','scorer'}
        for filename in ('scorer.py','synthetic_qualification.py'):
            tree=ast.parse(Path(__file__).with_name(filename).read_text())
            for node in ast.walk(tree):
                if isinstance(node,ast.Import):self.assertTrue(all(x.name in allowed for x in node.names))
                if isinstance(node,ast.ImportFrom):self.assertIn(node.module,allowed)
    def test_geometry(self):
        g=scorer.geometry();self.assertEqual(len(g),76)
        self.assertEqual(g['tok.weight'],(8192,192));self.assertEqual(g['blocks.5.qkv.weight'],(576,192))
    def test_alignment(self):
        self.assertEqual(scorer.candidate_input([2,1,7],[3,9]),[2,1,7,3])
        self.assertEqual(scorer.candidate_input([2],[3]),[2])
    def test_types(self):
        for bad in (True,1.0,-1,8192):
            with self.assertRaises(ValueError):scorer.candidate_input([2,bad],[3])
    def test_unsupported_and_bos(self):
        for p,t in (([0],[3]),([2],[]),([2],[1]),([2],[2]),([],[3])):
            with self.assertRaises(ValueError):scorer.candidate_input(p,t)
    def test_length_boundary(self):
        self.assertEqual(len(scorer.candidate_input([2],[3]*63)),63)
        with self.assertRaises(ValueError):scorer.candidate_input([2,3],[3]*63)
    def test_primitive_source_count(self):
        tree=ast.parse(Path(synthetic_qualification.__file__).read_text())
        calls=[n for n in ast.walk(tree) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='check']
        self.assertEqual(len(calls),8)

if __name__=='__main__':unittest.main()
