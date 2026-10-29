"""A separate one-batch experiment, never an old-epoch continuation."""
import hashlib
import json
import math

SCHEMA = 'PUBLIC_RANK_FORK_IDENTITY_V1'
NAMESPACE = 'PUBLIC_BASE_RANK_FIXED_BATCH_V1'
OBJECTIVE = {'survival': 'original_unchanged', 'regularizer': 0.001,
             'ranking_margin': 1.0, 'ranking_weight': 1.0, 'gradient_clip': 1.0}
OPTIMIZER = {'name': 'AdamW', 'learning_rate': 0.001, 'betas': [0.9, 0.999],
             'epsilon': 1e-8, 'weight_decay': 0.0}


def need(value, message):
    if not value: raise ValueError(message)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def h64(value):
    return type(value) is str and len(value) == 64 and value != '0' * 64 and all(c in '0123456789abcdef' for c in value)


def same_typed(value, expected):
    if type(value) is not type(expected): return False
    if type(expected) is dict:
        return set(value) == set(expected) and all(same_typed(value[k], expected[k]) for k in expected)
    if type(expected) is list:
        return len(value) == len(expected) and all(same_typed(a, b) for a, b in zip(value, expected))
    return value == expected


def validate(identity, parent_validate):
    need(type(identity) is dict and set(identity) == {'schema', 'namespace', 'parent_identity',
         'parent_identity_sha256', 'parent_checkpoint_sha256', 'parent_rng_file_sha256',
         'parent_state_sha256', 'parent_rng_state_sha256', 'schedule', 'source_sha256',
         'selection_review_sha256', 'objective', 'optimizer', 'parent_adam_step', 'maximum_updates'}, 'closed fork identity')
    need(identity['schema'] == SCHEMA and identity['namespace'] == NAMESPACE, 'distinct fork namespace')
    parent = identity['parent_identity']
    need(parent['schema'] == 'PUBLIC_HEAD_FIXED97_EPOCH_V1', 'only fixed97 parent epoch')
    parent_schedule = parent_validate(parent)
    need(identity['parent_identity_sha256'] == digest(parent), 'exact parent identity')
    for key in ('parent_checkpoint_sha256', 'parent_rng_file_sha256', 'parent_state_sha256',
                'parent_rng_state_sha256', 'source_sha256', 'selection_review_sha256'):
        need(h64(identity[key]), 'explicit fork evidence hash')
    need(type(identity['parent_adam_step']) is int and identity['parent_adam_step'] == 25 and
         type(identity['maximum_updates']) is int and identity['maximum_updates'] == 1, 'fixed one update after global Adam25')
    need(identity['schedule'] == [parent_schedule[2]] and len(parent_schedule[2]) == 4,
         'exact original third batch, no result-based replacement')
    need(same_typed(identity['objective'], OBJECTIVE) and same_typed(identity['optimizer'], OPTIMIZER), 'fixed new objective and original optimizer')
    return identity['schedule']


def counters(identity, logical_update, parent_validate):
    validate(identity, parent_validate)
    need(type(logical_update) is int and logical_update in (0, 1), 'fork logical update0 or1 only')
    return {'logical_update': logical_update, 'adam_step': 25 + logical_update,
            'scene_cursor': 4 * logical_update}


def validate_history(history, logical_update):
    need(type(history) is list and len(history) == logical_update, 'new experiment history only, not global step length')
    if logical_update == 0: return
    row = history[0]
    need(type(row) is dict and set(row) == {'logical_update', 'adam_step', 'scenes', 'conditions',
          'loss', 'gradient_norm', 'ranking_applied_conditions', 'ranking_ineligible_conditions'}, 'closed fork history entry')
    need(type(row['logical_update']) is int and row['logical_update'] == 1 and type(row['adam_step']) is int and row['adam_step'] == 26 and type(row['scenes']) is int and row['scenes'] == 4, 'one complete four-scene update')
    need(type(row['conditions']) is int and row['conditions'] in (8, 10, 12, 14, 16), 'all two-context eligible modes')
    need(all(type(row[k]) in (float, int) and math.isfinite(row[k]) and row[k] >= 0 for k in ('loss', 'gradient_norm')), 'finite nonnegative update statistics')
    need(all(type(row[k]) is int and row[k] >= 0 for k in ('ranking_applied_conditions', 'ranking_ineligible_conditions')) and
         row['ranking_applied_conditions'] + row['ranking_ineligible_conditions'] == row['conditions'], 'explicit ranking coverage accounting')
