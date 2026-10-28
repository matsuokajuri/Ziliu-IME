"""Synthetic, pure-stdlib store contracts. No real model data."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import checkpoint_store as store


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'checkpoint.json'
        self.payload = {'step': 3, 'synthetic': [1.25, None, True, '字']}

    def temps(self):
        return list(self.path.parent.glob('.checkpoint-*.tmp'))

    def write(self, body):
        self.path.write_bytes(body)
        return hashlib.sha256(body).hexdigest()

    def test_roundtrip_and_canonical(self):
        self.assertEqual(store.canonical({'z': 1, 'a': '字'}),
                         b'{"a":"\xe5\xad\x97","z":1}')
        sha = store.save_new(self.path, self.payload)
        self.assertEqual(sha, hashlib.sha256(self.path.read_bytes()).hexdigest())
        self.assertEqual(store.read_sealed(self.path, sha), self.payload)
        self.assertEqual(self.temps(), [])
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o600)

    def test…1099 tokens truncated…elf.assertRaises(OSError) as caught:
                store.save_new(self.path, self.payload)
        self.assertTrue(self.path.exists())
        self.assertEqual(len(self.temps()), 1)
        self.assertTrue(os.path.samefile(self.path, self.temps()[0]))
        self.assertIn('fsync-published-directory', '\n'.join(caught.exception.__notes__))
        body = self.path.read_bytes()
        self.assertEqual(store.read_sealed(self.path, hashlib.sha256(body).hexdigest()), self.payload)

    def test_prelink_file_fsync_failure(self):
        with patch.object(store.os, 'fsync', side_effect=OSError('synthetic temp failure')):
            with self.assertRaises(OSError):
                store.save_new(self.path, self.payload)
        self.assertFalse(self.path.exists())
        self.assertEqual(len(self.temps()), 1)


if __name__ == '__main__':
    unittest.main()
