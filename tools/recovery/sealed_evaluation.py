"""Fail-closed evaluation metadata checks; no model or private data included."""
import hashlib
import json
import os
import stat
from pathlib import Path

LIMIT = 16 * 1024 * 1024
FIELDS = ('scene_index', 'step', 'mode', 'context')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result


def _json(raw):
    def bad_constant(_):
        raise ValueError('nonfinite JSON constant')
    return json.loads(raw, object_pairs_hook=_pairs, parse_constant=bad_constant)


def _read(root, relative):
    require(isinstance(relative, str) and relative and '\\' not in relative,
            'relative POSIX name required')
    parts = relative.split('/')
    require(all(p not in ('', '.', '..') for p in parts), 'unsafe path')
    path = root
    require(not path.is_symlink() and path.is_dir(), 'real root required')
    for part in parts[:-1]:
        path = path / part
        require(not path.is_symlink() and path.is_dir(), 'real parent required')
    path = path / parts[-1]
    require(not path.is_symlink(), 'symlink rejected')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and before.st_size <= LIMIT, 'bounded regular file required')
        chunks = []
        size = 0
        while True:
            chunk = os.read(fd, min(1024 * 1024, LIMIT + 1 - size))
            if not chunk:
                break
            size += len(chunk)
            require(size <= LIMIT, 'file too large')
            chunks.append(chunk)
        after = os.fstat(fd)
        require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns)
                and size == before.st_size, 'file changed during read')
        return b''.join(chunks)
    finally:
        os.close(fd)


def _key(row):
    require(isinstance(row, dict) and all(k in row for k in FIELDS), 'condition metadata required')
    require(type(row['scene_index']) is int and row['scene_index'] >= 0
            and type(row['step']) is int and row['step'] >= 0
            and all(isinstance(row[k], str) and row[k] for k in ('mode', 'context')), 'condition types')
    return tuple(row[k] for k in FIELDS)


def _coverage(rows, expected):
    keys = [_key(row) for row in rows]
    wanted = [_key(row) for row in expected]
    require(len(wanted) == len(set(wanted)) and len(keys) == len(set(keys))
            and set(keys) == set(wanted), 'unique complete expected conditions required')


def verify_analysis(value, complete, plan_digest, window, expected):
    """Check identity, exact COMPLETE equality and condition coverage, not metrics."""
    require(type(window) is int and window >= 0, 'window type')
    require(canonical(value) == canonical(complete), 'analysis COMPLETE mismatch')
    require(value['plan_sha256'] == plan_digest and type(value['window']) is int
            and value['window'] == window, 'analysis identity mismatch')
    _coverage(value['metrics'], expected)


def verify_then_load(root, seal_sha256, plan_digest, expected_windows, target_loader):
    """Call target_loader once only after every expected window/menu verifies.

    Caller supplies trusted seal digest, plan digest, and ordered expected cells.
    This is not provenance validation or protection from a hostile concurrent writer.
    """
    root = Path(root)
    require(isinstance(expected_windows, list) and expected_windows, 'expected windows required')
    raw = _read(root, 'seal.json')
    require(hashlib.sha256(raw).hexdigest() == seal_sha256, 'seal hash mismatch')
    seal = _json(raw)
    require(set(seal) == {'plan_sha256', 'windows'} and seal['plan_sha256'] == plan_digest
            and len(seal['windows']) == len(expected_windows), 'seal scope mismatch')
    for index, expected in enumerate(expected_windows):
        base = f'window-{index:04d}'
        require(not os.path.lexists(root / base / 'UNKNOWN.json'), 'unknown window outcome')
        ref = seal['windows'][index]
        require(set(ref) == {'path', 'sha256'} and ref['path'] == base + '/receipt.json', 'window path mismatch')
        raw = _read(root, ref['path'])
        require(hashlib.sha256(raw).hexdigest() == ref['sha256'], 'receipt hash mismatch')
        receipt = _json(raw)
        complete = _json(_read(root, base + '/COMPLETE.json'))
        require(canonical(receipt) == canonical(complete), 'generation COMPLETE mismatch')
        require(set(receipt) == {'plan_sha256', 'window', 'menus'} and receipt['plan_sha256'] == plan_digest
                and type(receipt['window']) is int and receipt['window'] == index, 'receipt identity mismatch')
        _coverage(receipt['menus'], expected)
        names = set()
        for entry in receipt['menus']:
            require(set(entry) == set(FIELDS) | {'file', 'sha256'}, 'closed menu reference')
            name = entry['file']
            require(isinstance(name, str) and name not in ('', '.', '..')
                    and '/' not in name and '\\' not in name and name not in names, 'unique menu basename required')
            names.add(name)
            raw = _read(root, base + '/menus/' + name)
            require(hashlib.sha256(raw).hexdigest() == entry['sha256'], 'menu hash mismatch')
            require(_key(_json(raw)) == _key(entry), 'menu identity mismatch')
    return target_loader()
