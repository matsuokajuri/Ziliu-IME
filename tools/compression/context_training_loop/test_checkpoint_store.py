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

    def test_no_overwrite_retains_failure_evidence(self):
        sha = store.save_new(self.path, self.payload)
        original = self.path.read_bytes()
        with self.assertRaises(FileExistsError):
            store.save_new(self.path, {'different': True})
        self.assertEqual(self.path.read_bytes(), original)
        self.assertEqual(store.read_sealed(self.path, sha), self.payload)
        self.assertEqual(len(self.temps()), 1)

    def test_missing_wrong_and_absent_sha(self):
        with self.assertRaises(FileNotFoundError):
            store.read_sealed(self.path, '0' * 64)
        sha = store.save_new(self.path, self.payload)
        for bad in (None, '', 'x' * 64, sha.upper(), '0' * 64):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                store.read_sealed(self.path, bad)
        with self.assertRaises(TypeError):
            store.read_sealed(self.path)

    def test_tamper(self):
        sha = store.save_new(self.path, self.payload)
        self.path.write_bytes(self.path.read_bytes() + b' ')
        with self.assertRaises(ValueError):
            store.read_sealed(self.path, sha)

    def test_envelope_schema_payload_hash_and_canonical(self):
        envelope = {'schema': store.SCHEMA, 'payload': self.payload,
                    'payload_sha256': store.digest(self.payload)}
        for mutation in ('schema', 'hash', 'extra', 'format', 'duplicate'):
            candidate = dict(envelope)
            if mutation == 'schema': candidate['schema'] = 'unknown'
            if mutation == 'hash': candidate['payload_sha256'] = '0' * 64
            if mutation == 'extra': candidate['unexpected'] = 0
            body = store.canonical(candidate)
            if mutation == 'format': body += b'\n'
            if mutation == 'duplicate': body = b'{"schema":"ignored",' + body[1:]
            sha = self.write(body)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                store.read_sealed(self.path, sha)

    def test_symlink_read_and_publication_fail_closed(self):
        target = self.path.parent / 'original'
        target.write_bytes(b'original')
        self.path.symlink_to(target)
        with self.assertRaises(OSError):
            store.read_sealed(self.path, hashlib.sha256(b'original').hexdigest())
        with self.assertRaises(FileExistsError):
            store.save_new(self.path, self.payload)
        self.assertEqual(target.read_bytes(), b'original')
        self.assertTrue(self.path.is_symlink())

    def test_nonregular_and_size(self):
        self.path.mkdir()
        with self.assertRaises((ValueError, OSError)):
            store.read_sealed(self.path, '0' * 64)
        alternate = self.path.parent / 'large'
        alternate.write_bytes(b'12345')
        with patch.object(store, 'MAX_BYTES', 4):
            with self.assertRaises(ValueError):
                store.read_sealed(alternate, '0' * 64)
            with self.assertRaises(ValueError):
                store.save_new(self.path.parent / 'new', self.payload)

    def test_nan_and_nonjson(self):
        for value in (float('nan'), float('inf'), -float('inf'), {1: 'bad'}, (1,), b'bad'):
            with self.subTest(value=repr(value)), self.assertRaises(ValueError):
                store.save_new(self.path, value)
        self.assertFalse(self.path.exists())
        self.assertEqual(self.temps(), [])

    def test_prelink_interruption_keeps_temp_no_final(self):
        with patch.object(store.os, 'link', side_effect=KeyboardInterrupt('synthetic')):
            with self.assertRaises(KeyboardInterrupt):
                store.save_new(self.path, self.payload)
        self.assertFalse(self.path.exists())
        self.assertEqual(len(self.temps()), 1)
        body = self.temps()[0].read_bytes()
        self.assertEqual(store.read_sealed(self.temps()[0], hashlib.sha256(body).hexdigest()), self.payload)

    def test_postlink_uncertainty_keeps_both_no_retry(self):
        real_fsync = os.fsync
        calls = []
        def fail_directory(fd):
            calls.append(fd)
            if len(calls) == 2:
                raise OSError('synthetic post-link fsync uncertainty')
            real_fsync(fd)
        with patch.object(store.os, 'fsync', side_effect=fail_directory):
            with self.assertRaises(OSError) as caught:
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
