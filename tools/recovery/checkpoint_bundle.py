"""Local-only, hash-verified recovery capsules. Never uploads or executes content.

The expected archive SHA-256 must be retained independently of the capsule.
Integrity does not establish trusted provenance or model quality.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import zipfile

SCHEMA = 1
MAX_FILES = 10000
MAX_BYTES = 4 * 1024 ** 3


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_name(name: str) -> str:
    path = PurePosixPath(name)
    if (not name or name != str(path) or path.is_absolute() or '\\' in name
            or any(x in ('', '.', '..') for x in name.split('/'))
            or ':' in name or name == 'MANIFEST.json'):
        raise ValueError('unsafe or reserved relative file name')
    return name


def file_digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def create(destination: Path, files: dict[str, Path]) -> str:
    """Create a new capsule with explicit file selection; never replace a backup."""
    destination = Path(destination)
    if not files or len(files) > MAX_FILES:
        raise ValueError('invalid file count')
    for name, source in files.items():
        safe_name(name)
        if source.is_symlink() or not source.is_file():
            raise ValueError('source must be a regular non-symlink file')
    if sum(p.stat().st_size for p in files.values()) > MAX_BYTES:
        raise ValueError('capsule exceeds size limit')
    fd, temporary = tempfile.mkstemp(prefix='.capsule-', dir=destination.parent)
    try:
        records = {}
        total_bytes = 0
        with os.fdopen(fd, 'wb') as raw:
            with zipfile.ZipFile(raw, 'w', compression=zipfile.ZIP_STORED) as archive:
                for name, source in sorted(files.items()):
                    # Record exactly the bytes written, not a separate earlier read.
                    h = hashlib.sha256()
                    count = 0
                    with source.open('rb') as stream, archive.open(name, 'w') as member:
                        for block in iter(lambda: stream.read(1024 * 1024), b''):
                            count += len(block)
                            total_bytes += len(block)
                            if total_bytes > MAX_BYTES:
                                raise ValueError('source grew beyond size limit')
                            h.update(block)
                            member.write(block)
                    records[name] = {'bytes': count, 'sha256': h.hexdigest()}
                archive.writestr('MANIFEST.json', json.dumps(
                    {'schema': SCHEMA, 'files': records}, sort_keys=True).encode())
            raw.flush()
            os.fsync(raw.fileno())
        expected = file_digest(Path(temporary))
        verify(Path(temporary), expected)
        # Same-filesystem link is atomic and refuses an existing destination.
        os.link(temporary, destination)
        directory = os.open(destination.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        return expected
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def verify(capsule: Path, expected_sha256: str) -> dict:
    """Check outer hash, exact membership, safe names, sizes and every byte hash."""
    if (len(expected_sha256) != 64
            or any(c not in '0123456789abcdef' for c in expected_sha256)):
        raise ValueError('a lowercase independently retained SHA-256 is required')
    if file_digest(capsule) != expected_sha256:
        raise ValueError('capsule SHA-256 mismatch')
    with zipfile.ZipFile(capsule) as archive:
        infos = archive.infolist()
        names = [i.filename for i in infos]
        if len(names) != len(set(names)) or len(names) > MAX_FILES + 1:
            raise ValueError('duplicate members or too many files')
        if sum(i.file_size for i in infos if i.filename != 'MANIFEST.json') > MAX_BYTES:
            raise ValueError('expanded capsule exceeds size limit')
        for info in infos:
            if info.filename != 'MANIFEST.json':
                safe_name(info.filename)
            if info.is_dir() or stat.S_ISLNK(info.external_attr >> 16):
                raise ValueError('directories and symlinks are not capsule members')
        manifest_info = archive.getinfo('MANIFEST.json')
        if manifest_info.file_size > 4 * 1024 * 1024:
            raise ValueError('oversize manifest')
        manifest = json.loads(archive.read('MANIFEST.json'))
        if (not isinstance(manifest, dict) or manifest.get('schema') != SCHEMA
                or not isinstance(manifest.get('files'), dict)):
            raise ValueError('unsupported manifest')
        records = manifest['files']
        if not records or set(names) != set(records) | {'MANIFEST.json'}:
            raise ValueError('unexpected or missing members')
        for name, record in records.items():
            safe_name(name)
            h = hashlib.sha256()
            count = 0
            with archive.open(name) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    h.update(block)
                    count += len(block)
            if record != {'bytes': count, 'sha256': h.hexdigest()}:
                raise ValueError('member integrity mismatch')
    return manifest


def restore(capsule: Path, expected_sha256: str, destination: Path) -> dict:
    """Verify first, restore into a NEW directory, never execute restored files.

    On normal exceptions remove only the directory this call created. A killed
    process may leave an incomplete directory; absence of RESTORE-COMPLETE.json
    means it is not a completed restoration and must not be used for resumption.
    """
    manifest = verify(capsule, expected_sha256)
    if 'RESTORE-COMPLETE.json' in manifest['files']:
        raise ValueError('reserved restoration marker')
    destination.mkdir(mode=0o700)  # Atomic refusal of an existing directory or symlink.
    try:
        with zipfile.ZipFile(capsule) as archive:
            for name, record in manifest['files'].items():
                target = destination / name
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                with archive.open(name) as source, os.fdopen(
                        os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as out:
                    shutil.copyfileobj(source, out, length=1024 * 1024)
                    out.flush()
                    os.fsync(out.fileno())
                if file_digest(target) != record['sha256']:
                    raise ValueError('post-write integrity mismatch')
        # Persist newly created directory entries before publishing the marker.
        directories = [p for p in destination.rglob('*') if p.is_dir()]
        for directory in sorted(directories, key=lambda p: len(p.parts), reverse=True) + [destination]:
            fd = os.open(directory, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        marker = {'schema': SCHEMA, 'capsule_sha256': expected_sha256,
                  'file_count': len(manifest['files'])}
        with os.fdopen(os.open(destination / 'RESTORE-COMPLETE.json',
                               os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as out:
            json.dump(marker, out, sort_keys=True)
            out.flush()
            os.fsync(out.fileno())
        for directory in (destination, destination.parent):
            fd = os.open(directory, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        return marker
    except BaseException:
        shutil.rmtree(destination)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    pack = commands.add_parser('create')
    pack.add_argument('capsule', type=Path)
    pack.add_argument('files', nargs='+', help='relative/member=local-source')
    for command in ('verify', 'restore'):
        p = commands.add_parser(command)
        p.add_argument('capsule', type=Path)
        p.add_argument('--sha256', required=True)
        if command == 'restore':
            p.add_argument('destination', type=Path)
    args = parser.parse_args()
    if args.command == 'create':
        files = {}
        for item in args.files:
            name, source = item.split('=', 1)
            if name in files:
                parser.error('duplicate member name')
            files[name] = Path(source)
        print(create(args.capsule, files))
    elif args.command == 'verify':
        manifest = verify(args.capsule, args.sha256)
        print(json.dumps({'verified_files': len(manifest['files'])}))
    else:
        print(json.dumps(restore(args.capsule, args.sha256, args.destination)))


if __name__ == '__main__':
    main()
