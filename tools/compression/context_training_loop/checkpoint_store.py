"""Canonical JSON checkpoint envelopes; no model dependencies or implicit I/O.

A failed save is NOT permission to retry: inspect the final name and retained
private temporary file. In particular fsync failures after link are uncertain.
Payload semantic/model-state validation belongs to the caller.
"""
import hashlib
import json
import math
import os
import re
import secrets
import stat

MAX_BYTES = 64 * 1024 * 1024
SCHEMA = 'ziliu.generic-checkpoint-envelope.v1'


def _json_value(value):
    if value is None or type(value) in (str, bool, int):
        return
    if type(value) is float and math.isfinite(value):
        return
    if type(value) is list:
        for item in value:
            _json_value(item)
        return
    if type(value) is dict and all(type(k) is str for k in value):
        for item in value.values():
            _json_value(item)
        return
    raise ValueError('Only finite JSON values and string dictionary keys allowed')


def canonical(value):
    """Strict JSON: UTF-8, sorted keys, compact separators, no trailing newline."""
    _json_value(value)
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode('utf-8')


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _sha(value):
    if type(value) is not str or re.fullmatch(r'[0-9a-f]{64}', value) is None:
        raise ValueError('Expected an explicit lowercase SHA-256')


def _parent(path):
    path = os.fspath(path)
    if type(path) is not str:
        raise TypeError('Checkpoint path must be text')
    parent, name = os.path.split(path)
    if name in ('', '.', '..'):
        raise ValueError('Expected a checkpoint file name')
    fd = os.open(parent or '.', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    return fd, name


def save_new(path, payload):
    """Publish a new sealed checkpoint, never replacing an existing final name.

    fsync private 0600 same-directory temp, hardlink final, then fsync directory.
    All failures retain temp evidence, if created. Exception notes identify it.
    A post-link failure means final durability is UNKNOWN; reconcile by readback.
    A successful save removes only its own temp and returns envelope-file SHA.
    No lock/retry, parent-directory creation, or caller semantic validation here.
    """
    body = canonical({'schema': SCHEMA, 'payload': payload,
                      'payload_sha256': digest(payload)})
    if len(body) > MAX_BYTES:
        raise ValueError('Checkpoint envelope exceeds 64 MiB')
    directory, name = _parent(path)
    temporary = '.checkpoint-' + secrets.token_hex(24) + '.tmp'
    stage = 'create-temp'
    created = False
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                     os.O_NOFOLLOW, 0o600, dir_fd=directory)
        created = True
        with os.fdopen(fd, 'wb') as stream:
            stage = 'write-temp'
            stream.write(body)
            stream.flush()
            stage = 'fsync-temp'
            os.fsync(stream.fileno())
        stage = 'link-final'
        os.link(temporary, name, src_dir_fd=directory, dst_dir_fd=directory,
                follow_symlinks=False)
        stage = 'fsync-published-directory'
        os.fsync(directory)
        # Durable final is established. Remove only the temp created above.
        stage = 'cleanup-owned-temp'
        os.unlink(temporary, dir_fd=directory)
        return hashlib.sha256(body).hexdigest()
    except BaseException as error:
        if hasattr(error, 'add_note'):
            error.add_note('checkpoint save stage=' + stage + '; temporary=' +
                           temporary + '; created=' + str(created) +
                           '; reconcile final before any retry')
        raise
    finally:
        os.close(directory)


def read_sealed(path, expected_sha):
    """Read regular non-symlink file, max64MiB, validating both hashes/schema.

    expected_sha is the SHA of exact complete canonical envelope bytes, not
    payload_sha256. Does not follow final or immediate parent symlinks. Ancestor
    directory trust is the caller's responsibility. No pickle/NumPy/deserialization
    beyond JSON is performed.
    """
    _sha(expected_sha)
    directory, name = _parent(path)
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK,
                     dir_fd=directory)
        with os.fdopen(fd, 'rb') as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_BYTES:
                raise ValueError('Checkpoint must be regular and at most 64 MiB')
            body = stream.read(MAX_BYTES + 1)
            if len(body) > MAX_BYTES:
                raise ValueError('Checkpoint exceeds 64 MiB')
    finally:
        os.close(directory)
    if hashlib.sha256(body).hexdigest() != expected_sha:
        raise ValueError('Checkpoint file SHA mismatch')
    envelope = json.loads(body.decode('utf-8'))
    if type(envelope) is not dict or set(envelope) != {'schema', 'payload', 'payload_sha256'}:
        raise ValueError('Invalid checkpoint envelope fields')
    if envelope['schema'] != SCHEMA:
        raise ValueError('Invalid checkpoint schema')
    if canonical(envelope) != body:
        raise ValueError('Noncanonical checkpoint envelope')
    _sha(envelope['payload_sha256'])
    if digest(envelope['payload']) != envelope['payload_sha256']:
        raise ValueError('Checkpoint payload SHA mismatch')
    return envelope['payload']
