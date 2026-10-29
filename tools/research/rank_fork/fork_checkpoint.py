"""Immutable fork NPZ/RNG pair with independent logical and global Adam counters."""
import json
import hashlib
import random
import base64
import os
import tempfile
from pathlib import Path


def optimizer_group(opt, contract):
    import torch
    contract.need(isinstance(opt, torch.optim.AdamW) and len(opt.param_groups) == 1, 'one original AdamW group')
    group = {k: v for k, v in opt.param_groups[0].items() if k != 'params'}
    contract.need(group['lr'] == .001 and group['betas'] == (.9, .999) and group['eps'] == 1e-8 and group['weight_decay'] == 0 and
                  all(group.get(k) is False for k in ('amsgrad', 'maximize', 'capturable', 'differentiable')), 'original AdamW hyperparameters')
    return json.loads(json.dumps(group, allow_nan=False))


def array_seal(array):
    return {'shape': list(array.shape), 'dtype': str(array.dtype),
            'sha256': hashlib.sha256(array.tobytes()).hexdigest()}


def validate_arrays(parameter, data, state, adam_step, np, contract):
    contract.need(data.dtype == np.float32 and data.shape == tuple(parameter.shape) and np.isfinite(data).all(), 'finite FP32 parameter')
    contract.need(set(state) == {'step', 'exp_avg', 'exp_avg_sq'}, 'complete Adam slots')
    for key, value in state.items():
        contract.need(value.dtype == np.float32 and value.shape == (() if key == 'step' else tuple(parameter.shape)) and np.isfinite(value).all(), 'finite FP32 Adam shape')
        if key == 'step': contract.need(value.item() == adam_step, 'global Adam25/26')
        if key == 'exp_avg_sq': contract.need((value >= 0).all(), 'nonnegative Adam variance')


def preflight_rng(rng, torch, np, contract, op):
    contract.need(type(rng) is dict and set(rng) == {'python', 'numpy', 'torch_cpu'}, 'closed RNG')
    def tuples(value): return tuple(tuples(x) for x in value) if type(value) is list else value
    local_python = random.Random(); local_python.setstate(tuples(rng['python']))
    contract.need(op.digest(local_python.getstate()) == op.digest(rng['python']), 'Python RNG exact preflight')
    n = rng['numpy']
    contract.need(type(n) is dict and set(n) == {'algorithm', 'keys', 'position', 'has_gauss', 'cached_gaussian'} and n['algorithm'] == 'MT19937' and
                  type(n['keys']) is list and len(n['keys']) == 624 and all(type(x) is int and 0 <= x < 2**32 for x in n['keys']) and
                  type(n['position']) is int and 0 <= n['position'] <= 624 and type(n['has_gauss']) is int and n['has_gauss'] in (0, 1), 'NumPy RNG fields')
    local_numpy = np.random.RandomState()
    local_numpy.set_state((n['algorithm'], np.asarray(n['keys'], dtype=np.uint32), n['position'], n['has_gauss'], n['cached_gaussian']))
    actual = local_numpy.get_state()
    check = {'algorithm': actual[0], 'keys': actual[1].tolist(), 'position': actual[2], 'has_gauss': actual[3], 'cached_gaussian': actual[4]}
    contract.need(op.digest(check) == op.digest(n), 'NumPy RNG exact preflight')
    raw = base64.b64decode(rng['torch_cpu'], validate=True)
    contract.need(0 < len(raw) <= 65536, 'Torch RNG bounded')
    local_torch = torch.Generator(device='cpu')
    local_torch.set_state(torch.from_numpy(np.frombuffer(raw, dtype=np.uint8).copy()))
    contract.need(local_torch.get_state().numpy().tobytes() == raw, 'Torch RNG exact preflight')


def optimizer_parameters(model, opt, contract):
    expected = list(model.parameters())
    contract.need(len(opt.param_groups) == 1 and len(opt.param_groups[0]['params']) == len(expected) and
                  all(a is b for a, b in zip(opt.param_groups[0]['params'], expected)), 'optimizer covers exact model parameters in original order')


def save_pair(root, identity, logical_update, history, previous_sha256,
              model, opt, torch, np, contract, parent_validate, op):
    optimizer_parameters(model, opt, contract)
    count = contract.counters(identity, logical_update, parent_validate)
    contract.validate_history(history, logical_update)
    contract.need(contract.h64(previous_sha256), 'explicit immutable previous checkpoint')
    if logical_update == 0:
        contract.need(previous_sha256 == identity['parent_checkpoint_sha256'], 'migration binds exact parent pair')
        contract.need(op.digest(op.state_seal(model, opt)) == identity['parent_state_sha256'] and
                      op.digest(op.rng_snapshot(torch, np)) == identity['parent_rng_state_sha256'], 'migration preserves original full state and RNG')
    group = optimizer_group(opt, contract)
    arrays = {}; names = [name for name, _ in model.named_parameters()]
    for name, parameter in model.named_parameters():
        arrays['w/' + name] = parameter.detach().cpu().numpy().copy()
        state = opt.state[parameter]
        contract.need(set(state) == {'step', 'exp_avg', 'exp_avg_sq'} and state['step'].item() == count['adam_step'], 'global Adam state counter')
        numeric_state = {key: value.detach().cpu().numpy().copy() for key, value in state.items()}
        validate_arrays(parameter, arrays['w/' + name], numeric_state, count['adam_step'], np, contract)
        for key, value in numeric_state.items(): arrays['o/' + name + '/' + key] = value
    seal = op.state_seal(model, opt); rng = op.rng_snapshot(torch, np)
    preflight_rng(rng, torch, np, contract, op)
    meta = {'schema': 'PUBLIC_RANK_FORK_CHECKPOINT_V1', 'identity': identity, **count,
            'history': history, 'previous_checkpoint_sha256': previous_sha256,
            'parameter_names': names, 'optimizer_group': group, 'state_seal': seal,
            'rng_state_sha256': op.digest(rng)}
    arrays['metadata'] = np.array(json.dumps(meta, sort_keys=True, allow_nan=False))
    root = Path(root); root.mkdir(mode=0o700, parents=False, exist_ok=True)
    path = root / ('fork-%03d.npz' % logical_update)
    side_path = root / ('fork-%03d-rng.json' % logical_update)
    contract.need(not path.exists() and not side_path.exists(), 'immutable pair paths')
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.pending-', dir=root)
    try:
        with os.fdopen(fd, 'wb') as stream:
            np.savez(stream, **arrays); stream.flush(); os.fsync(stream.fileno())
        with np.load(temporary, allow_pickle=False) as z:
            contract.need(len(z.files) == len(set(z.files)) and set(z.files) == set(arrays) and all(np.array_equal(z[k], v) for k, v in arrays.items()), 'exact prepared archive readback')
        os.link(temporary, path); os.unlink(temporary)
        fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
        try: os.fsync(fd)
        finally: os.close(fd)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)
    checkpoint_sha = op.file_sha(path)
    op.put(side_path, {'schema': 'PUBLIC_RANK_FORK_RNG_V1', 'identity_sha256': op.digest(identity),
           **count, 'checkpoint_file_sha256': checkpoint_sha, 'rng': rng, 'state_seal': seal})
    return {key: {'path': str(p), 'bytes': p.stat().st_size, 'sha256': op.file_sha(p)}
            for key, p in [('checkpoint', path), ('rng', side_path)]}


def restore_pair(pair, identity, model, opt, torch, np, contract, parent_validate, op, *, expected_previous_sha256):
    contract.validate(identity, parent_validate)
    optimizer_parameters(model, opt, contract)
    contract.need(contract.h64(expected_previous_sha256), 'explicit expected predecessor')
    contract.need(type(pair) is dict and set(pair) == {'checkpoint', 'rng'}, 'closed pair')
    for ref in pair.values():
        contract.need(type(ref) is dict and set(ref) == {'path', 'bytes', 'sha256'} and
                      type(ref['bytes']) is int and 0 < ref['bytes'] <= 2 * 2**20 and
                      Path(ref['path']).stat().st_size == ref['bytes'] and op.file_sha(ref['path']) == ref['sha256'], 'exact bounded pair')
    side = json.loads(Path(pair['rng']['path']).read_bytes())
    contract.need(set(side) == {'schema', 'identity_sha256', 'logical_update', 'adam_step', 'scene_cursor', 'checkpoint_file_sha256', 'rng', 'state_seal'} and
                  side['schema'] == 'PUBLIC_RANK_FORK_RNG_V1' and side['identity_sha256'] == op.digest(identity) and side['checkpoint_file_sha256'] == pair['checkpoint']['sha256'], 'fork sidecar binding')
    count = contract.counters(identity, side['logical_update'], parent_validate)
    contract.need(all(side[k] == v and type(side[k]) is int for k, v in count.items()), 'sidecar counters')
    with np.load(pair['checkpoint']['path'], allow_pickle=False) as z:
        meta = json.loads(str(z['metadata']))
        contract.need(set(meta) == {'schema', 'identity', 'logical_update', 'adam_step', 'scene_cursor', 'history', 'previous_checkpoint_sha256', 'parameter_names', 'optimizer_group', 'state_seal', 'rng_state_sha256'} and meta['schema'] == 'PUBLIC_RANK_FORK_CHECKPOINT_V1' and op.digest(meta['identity']) == op.digest(identity), 'fork metadata namespace and identity')
        contract.need(all(meta[k] == v and type(meta[k]) is int for k, v in count.items()), 'metadata counters')
        contract.validate_history(meta['history'], count['logical_update'])
        contract.need(meta['previous_checkpoint_sha256'] == expected_previous_sha256, 'exact externally expected predecessor')
        if count['logical_update'] == 0:
            contract.need(meta['previous_checkpoint_sha256'] == identity['parent_checkpoint_sha256'], 'migration predecessor')
            contract.need(op.digest(meta['state_seal']) == identity['parent_state_sha256'] and meta['rng_state_sha256'] == identity['parent_rng_state_sha256'], 'restored migration remains exact parent state and RNG')
        contract.need(op.digest(meta['optimizer_group']) == op.digest(optimizer_group(opt, contract)), 'exact inherited optimizer hyperparameters')
        names = [n for n, _ in model.named_parameters()]
        contract.need(meta['parameter_names'] == names and meta['state_seal'] == side['state_seal'], 'exact parameter names and seal binding')
        contract.need(op.state_seal(model, opt)['embedding'] == meta['state_seal']['embedding'], 'external frozen embedding unchanged')
        expected = {'metadata'}; staged = []
        for name, parameter in model.named_parameters():
            data = z['w/' + name]; expected.add('w/' + name)
            contract.need(data.dtype == np.float32 and data.shape == tuple(parameter.shape) and np.isfinite(data).all(), 'finite FP32 parameter')
            state = {}
            for key in ('step', 'exp_avg', 'exp_avg_sq'):
                field = 'o/' + name + '/' + key; value = np.array(z[field], copy=True); expected.add(field)
                contract.need(value.dtype == np.float32 and value.shape == (() if key == 'step' else tuple(parameter.shape)) and np.isfinite(value).all(), 'finite FP32 Adam shape')
                if key == 'step': contract.need(value.item() == count['adam_step'], 'Adam25/26 distinct from logical0/1')
                if key == 'exp_avg_sq': contract.need((value >= 0).all(), 'nonnegative Adam variance')
                state[key] = torch.from_numpy(value)
            staged.append((parameter, np.array(data, copy=True), state))
        contract.need(len(z.files) == len(set(z.files)) and set(z.files) == expected, 'closed checkpoint fields')
        staged_seal = {'parameters': {}, 'adam': {}, 'embedding': op.state_seal(model, opt)['embedding']}
        for name, (parameter, data, state) in zip(names, staged):
            staged_seal['parameters'][name] = array_seal(data)
            staged_seal['adam'][name] = {key: array_seal(value.numpy()) for key, value in state.items()}
        contract.need(staged_seal == meta['state_seal'], 'all arrays exactly match sealed metadata before mutation')
    contract.need(op.digest(side['rng']) == meta['rng_state_sha256'], 'RNG metadata binding')
    preflight_rng(side['rng'], torch, np, contract, op)
    contract.need(all(op.file_sha(ref['path']) == ref['sha256'] for ref in pair.values()), 'pair unchanged before application')
    # Rollback protects even unexpected exceptions during the final application.
    prior_parameters = [(p, p.detach().clone()) for p in model.parameters()]
    prior_slots = {p: {key: value.detach().clone() for key, value in opt.state.get(p, {}).items()} for p in model.parameters()}
    prior_membership = {p: p in opt.state for p in model.parameters()}
    prior_rng = op.rng_snapshot(torch, np)
    try:
        for parameter, data, state in staged:
            with torch.no_grad(): parameter.copy_(torch.from_numpy(data))
            opt.state[parameter] = state
        op.restore_rng(side['rng'], torch, np)
        contract.need(op.state_seal(model, opt) == meta['state_seal'] and op.digest(op.rng_snapshot(torch, np)) == meta['rng_state_sha256'], 'exact full-state restore')
        contract.need(all(op.file_sha(ref['path']) == ref['sha256'] for ref in pair.values()), 'pair remains read-only')
    except BaseException:
        for parameter, data in prior_parameters:
            with torch.no_grad(): parameter.copy_(data)
            if prior_membership[parameter]: opt.state[parameter] = prior_slots[parameter]
            else: opt.state.pop(parameter, None)
        op.restore_rng(prior_rng, torch, np)
        raise
    return meta
