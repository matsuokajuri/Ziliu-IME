import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import checkpoint_bundle as bundle


class CapsuleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'state.bin'
        self.source.write_bytes(b'synthetic checkpoint bytes\x00\x01')
        self.archive = self.root / 'backup.zip'

    def create(self):
        return bundle.create(self.archive, {'state/checkpoint.bin': self.source})

    def crafted(self, files):
        with zipfile.ZipFile(self.archive, 'w') as z:
            for name, content in files:
                z.writestr(name, content)
        return bundle.file_digest(self.archive)

    def test_roundtrip_new_directory_and_independent_hash(self):
        sha = self.create()
        self.assertEqual(sha, hashlib.sha256(self.archive.read_bytes()).hexdigest())
        target = self.root / 'restored'
        result = bundle.restore(self.archive, sha, target)
        self.assertEqual((target / 'state/checkpoint.bin').read_bytes(), self.source.read_bytes())
        self.assertEqual(result['file_count'], 1)
        self.assertTrue((target / 'RESTORE-COMPLETE.json').is_file())

    def test_create_never_overwrites_existing_backup(self):
        self.create()
        old = self.archive.read_bytes()
        self.source.write_bytes(b'new data')
        with self.assertRaises(FileExistsError):
            self.create()
        self.assertEqual(old, self.archive.read_bytes())
        self.assertEqual(list(self.root.glob('.capsule-*')), [])

    def test_restore_never_overwrites_existing_work(self):
        sha = self.create()
        target = self.root / 'restored'
        target.mkdir()
        (target / 'uncommitted').write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            bundle.restore(self.archive, sha, target)
        self.assertEqual((target / 'uncommitted').read_bytes(), b'keep')

    def test_corrupt_outer_rejected_before_creating_destination(self):
        sha = self.create()
        self.archive.write_bytes(self.archive.read_bytes() + b'corrupt')
        target = self.root / 'restored'
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            bundle.restore(self.archive, sha, target)
        self.assertFalse(target.exists())

    def test_wrong_member_hash_rejected_even_with_correct_outer_hash(self):
        manifest = {'schema': 1, 'files': {'state.bin': {'bytes': 3, 'sha256': '0' * 64}}}
        sha = self.crafted([('state.bin', b'bad'), ('MANIFEST.json', json.dumps(manifest))])
        with self.assertRaisesRegex(ValueError, 'member integrity'):
            bundle.verify(self.archive, sha)

    def test_traversal_absolute_and_noncanonical_names_rejected(self):
        for name in ('../escape', '/absolute', 'a//b', 'a/./b', 'a\\b', 'C:/file'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                bundle.safe_name(name)

    def test_duplicate_archive_entries_rejected(self):
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            sha = self.crafted([('state', b'a'), ('state', b'b'), ('MANIFEST.json', '{}')])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            bundle.verify(self.archive, sha)

    def test_symlink_source_rejected(self):
        link = self.root / 'link'
        link.symlink_to(self.source)
        with self.assertRaisesRegex(ValueError, 'non-symlink'):
            bundle.create(self.archive, {'state': link})

    def test_unexpected_member_rejected(self):
        sha = self.crafted([('extra', b'x'), ('MANIFEST.json', '{"schema":1,"files":{}}')])
        with self.assertRaisesRegex(ValueError, 'unexpected or missing'):
            bundle.verify(self.archive, sha)

    def test_write_failure_cleans_only_new_restore_directory(self):
        sha = self.create()
        target = self.root / 'restored'
        with patch.object(bundle.shutil, 'copyfileobj', side_effect=OSError('synthetic disk error')):
            with self.assertRaises(OSError):
                bundle.restore(self.archive, sha, target)
        self.assertFalse(target.exists())
        self.assertTrue(self.archive.exists())
        self.assertTrue(self.source.exists())

    def test_resumption_marker_cannot_be_supplied_by_capsule(self):
        sha = bundle.create(self.archive, {'RESTORE-COMPLETE.json': self.source})
        with self.assertRaisesRegex(ValueError, 'reserved'):
            bundle.restore(self.archive, sha, self.root / 'restored')

    def test_cumulative_growth_limit_enforced_during_copy(self):
        from io import BytesIO
        from types import SimpleNamespace
        from unittest.mock import Mock
        sources = {}
        for name in ('first', 'second'):
            source = Mock()
            source.is_symlink.return_value = False
            source.is_file.return_value = True
            source.stat.return_value = SimpleNamespace(st_size=0)
            source.open.return_value = BytesIO(b'1234')
            sources[name] = source
        with patch.object(bundle, 'MAX_BYTES', 6):
            with self.assertRaisesRegex(ValueError, 'grew beyond'):
                bundle.create(self.archive, sources)
        self.assertFalse(self.archive.exists())
        self.assertEqual(list(self.root.glob('.capsule-*')), [])

    def test_restore_defaults_private_even_with_permissive_umask(self):
        import os
        import stat
        sha = self.create()
        target = self.root / 'restored'
        previous = os.umask(0)
        try:
            bundle.restore(self.archive, sha, target)
        finally:
            os.umask(previous)
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE((target / 'state/checkpoint.bin').stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE((target / 'RESTORE-COMPLETE.json').stat().st_mode), 0o600)

    def test_non_object_manifest_rejected(self):
        sha = self.crafted([('MANIFEST.json', '[]')])
        with self.assertRaisesRegex(ValueError, 'unsupported manifest'):
            bundle.verify(self.archive, sha)

    def test_incorrect_or_missing_expected_digest_rejected(self):
        self.create()
        for sha in ('', 'g' * 64, '0' * 64):
            with self.subTest(sha=sha), self.assertRaises(ValueError):
                bundle.verify(self.archive, sha)


if __name__ == '__main__':
    unittest.main()
