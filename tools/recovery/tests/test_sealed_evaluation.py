import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('guard', Path(__file__).parents[1] / 'sealed_evaluation.py')
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.calls = []
        self.cell = dict(scene_index=0, step=3, mode='synthetic', context='empty')
        self.expected = [[self.cell], [self.cell]]
        self.seal = dict(plan_sha256='invented-plan', windows=[])
        for i in range(2):
            base = f'window-{i:04d}'
            menu = dict(self.cell, payload='invented')
            h = self.put(base + '/menus/m.json', menu)
            receipt = dict(plan_sha256='invented-plan', window=i,
                           menus=[dict(self.cell, file='m.json', sha256=h)])
            h = self.put(base + '/receipt.json', receipt)
            self.put(base + '/COMPLETE.json', receipt)
            self.seal['windows'].append(dict(path=base + '/receipt.json', sha256=h))
        self.seal_hash = self.put('seal.json', self.seal)

    def put(self, path, value):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        raw = g.canonical(value)
        p.write_bytes(raw)
        return hashlib.sha256(raw).hexdigest()

    def run_guard(self):
        return g.verify_then_load(self.root, self.seal_hash, 'invented-plan', self.expected,
                                  lambda: self.calls.append(True) or 'loaded')

    def rejected(self):
        with self.assertRaises((ValueError, OSError, KeyError, TypeError)):
            self.run_guard()
        self.assertEqual(self.calls, [])

    def test_complete(self):
        self.assertEqual(self.run_guard(), 'loaded')
        self.assertEqual(self.calls, [True])

    def test_late_window_tamper_before_callback(self):
        (self.root / 'window-0001/menus/m.json').write_text('{}')
        self.rejected()

    def test_missing_complete(self):
        (self.root / 'window-0001/COMPLETE.json').unlink()
        self.rejected()

    def test_unknown(self):
        (self.root / 'window-0001/UNKNOWN.json').write_text('{}')
        self.rejected()

    def test_complete_mismatch(self):
        self.put('window-0001/COMPLETE.json', {})
        self.rejected()

    def test_missing_window(self):
        self.seal['windows'].pop()
        self.seal_hash = self.put('seal.json', self.seal)
        self.rejected()

    def test_duplicate_or_missing_expected(self):
        self.expected[1].append(self.cell)
        self.rejected()
        self.expected[1] = []
        self.rejected()

    def test_bad_seal_hash(self):
        self.seal_hash = '0' * 64
        self.rejected()

    def test_symlink_menu(self):
        p = self.root / 'window-0001/menus/m.json'
        p.unlink()
        p.symlink_to('../../window-0000/menus/m.json')
        self.rejected()

    def test_symlink_parent(self):
        p = self.root / 'window-0001/menus'
        p.rename(p.with_name('original'))
        p.symlink_to('original', target_is_directory=True)
        self.rejected()

    def test_traversal(self):
        for name in ('../x', '/x', 'a//x', 'a/./x', 'a/../x', 'a\\x'):
            with self.assertRaises(ValueError):
                g._read(self.root, name)

    def test_json_duplicate_nonfinite(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":Infinity}'):
            with self.assertRaises(ValueError):
                g._json(raw)

    def test_analysis(self):
        value = dict(plan_sha256='p', window=0, metrics=[self.cell])
        g.verify_analysis(value, value, 'p', 0, [self.cell])
        for bad in (dict(value, window=1), dict(value, metrics=[]),
                    dict(value, metrics=[self.cell, self.cell]), dict(value, window=False)):
            with self.assertRaises(ValueError):
                g.verify_analysis(bad, bad, 'p', 0, [self.cell])
        with self.assertRaises(ValueError):
            g.verify_analysis(value, dict(value, extra=True), 'p', 0, [self.cell])


if __name__ == '__main__':
    unittest.main()
