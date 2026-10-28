"""Synthetic-only byte materializer. No real-model namespace or dependency identities.
No filesystem loader, NumPy import, tokenizer, teacher, uploader or final-set API.
Caller supplies separately sealed identities; independent review remains necessary.
"""
import copy
import hashlib
import json
import math
import struct


def need(ok, message):
    if not ok:
        raise ValueError(message)

def sha(value):
    return type(value) is str and len(value) == 64 and value != '0' * 64 and all(c in '0123456789abcdef' for c in value)

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()

def character_vocabulary_rows(vocabulary, bos, unk):
    need(type(bos) is int and type(unk) is int and bos == 2 and unk == 1
         and list(vocabulary[:3]) == ['<pad>', '<unk>', '<bos>']
         and all(type(token) is str and len(token) == 1 for token in vocabulary[3:]),
         'conventional special rows and character vocabulary')

def character_tail_rows(ids, rows):
    need(type(rows) is int and rows >= 4, 'strict vocabulary size')
    need(all(type(i) is int and 3 <= i < rows for i in ids),
         'candidate tails contain character rows only')

def materialize(raw, vocabulary, descriptor, descriptor_seal, tokenization, tokenization_seal,
                expected_namespace, expected_order):
    """Return packed <f8 matrices and identity only, with PENDING review status.

    expected_order is an externally admitted four-pool list of dictionaries with
    id/candidate_keys/candidate_input_sha256s, not derived from tokenization here.
    Tokenization's exact input-to-token relationship is a separate review duty.
    """
    # Plain JSON snapshots avoid input aliasing; no injected digest/conversion callbacks.
    descriptor = json.loads(json.dumps(descriptor, allow_nan=False))
    tokenization = json.loads(json.dumps(tokenization, allow_nan=False))
    expected_order = copy.deepcopy(expected_order)
    need(digest(descriptor) == descriptor_seal and digest(tokenization) == tokenization_seal,
         'externally sealed descriptor and tokenization')
    need(type(raw) is bytes, 'immutable raw embedding bytes only')
    need(type(vocabulary) is list and all(type(t) is str for t in vocabulary), 'ordered vocabulary strings')
    vocabulary = tuple(vocabulary)
    need(set(descriptor) == {'schema', 'namespace', 'endpoint_sha256', 'tensor_name', 'tensor_sha256',
                            'shape', 'dtype', 'layout', 'vocabulary_sha256', 'tokenizer_source_sha256',
                            'bos_id', 'unk_id'}, 'exact descriptor schema')
    need(descriptor['schema'] == 'FROZEN_E_BINDING_V1', 'binding schema')
    need(expected_namespace == 'SYNTHETIC_QUALIFICATION_V1' and
         descriptor['namespace'] == expected_namespace, 'externally admitted namespace')
    need(all(sha(descriptor[k]) for k in ('endpoint_sha256', 'tensor_sha256', 'vocabulary_sha256',
                                        'tokenizer_source_sha256')), 'dependency identities')
    shape = descriptor['shape']
    need(type(shape) is list and len(shape) == 2 and all(type(n) is int and n > 0 for n in shape), 'strict dimensions')
    rows, width = shape
    need(descriptor['dtype'] == '<f4' and descriptor['layout'] == 'C' and descriptor['tensor_name'] == 'tok.weight',
         'exact FP32 C-order synthetic source')
    need(rows <= 32 and width <= 16, 'synthetic fixture only; no real data loader')
    need(len(raw) == rows * width * 4 and hashlib.sha256(raw).hexdigest() == descriptor['tensor_sha256'], 'exact E bytes/size')
    need(len(vocabulary) == rows and len(set(vocabulary)) == rows and
         digest(list(vocabulary)) == descriptor['vocabulary_sha256'], 'complete unique ordered vocabulary row identity')
    bos, unk = descriptor['bos_id'], descriptor['unk_id']
    need(type(bos) is int and type(unk) is int and 0 <= bos < rows and 0 <= unk < rows and bos != unk, 'sealed special token rows')
    need(all(math.isfinite(x[0]) for x in struct.iter_unpack('<f', raw)), 'all frozen E values finite')
    need(set(tokenization) == {'schema', 'namespace', 'split', 'binding_sha256', 'source_manifest_sha256',
                               'final_exclusion_policy_sha256', 'records'}, 'exact tokenization schema')
    need(tokenization['schema'] == 'COMPLETE_FOUR_POOL_TOKENIZATION_V1' and tokenization['namespace'] == expected_namespace
         and tokenization['split'] == 'TRAIN' and tokenization['binding_sha256'] == descriptor_seal,
         'TRAIN-only exact binding')
    need(sha(tokenization['source_manifest_sha256']) and sha(tokenization['final_exclusion_policy_sha256']), 'source/exclusion identities')
    records = tokenization['records']
    need(type(records) is list and len(records) == 4 and type(expected_order) is list and len(expected_order) == 4, 'four complete pools')
    need(all(type(r) is dict and set(r) == {'id', 'candidate_keys', 'candidate_input_sha256s'} for r in expected_order), 'admitted complete pool schema')
    need(len(set(r['id'] for r in expected_order)) == 4 and all(type(r['id']) is str and r['id'] for r in expected_order), 'unique four pool IDs')
    seen = {}; seen_inputs = {}; result = []
    def pack(ids, context):
        need(type(ids) is list and 1 <= len(ids) <= (64 if context else 63), 'complete bounded token sequence')
        need(all(type(i) is int and 0 <= i < rows for i in ids), 'exact integer token rows')
        if not context:
            need(all(i != unk for i in ids), 'candidate tails reject UNK without filtering')
        if context:
            need(ids[0] == bos, 'context includes sealed BOS row')
        payload = b''.join(struct.pack('<d', value[0]) for i in ids for value in
                           struct.iter_unpack('<f', raw[i*width*4:(i+1)*width*4]))
        return {'shape': [len(ids), width], 'dtype': '<f8', 'hex': payload.hex()}
    for r, expected in zip(records, expected_order):
        need(set(r) == {'id', 'candidate_keys', 'candidate_input_sha256s', 'context_token_ids', 'candidate_token_ids'}, 'exact token record')
        for key in ('id', 'candidate_keys', 'candidate_input_sha256s'):
            need(r[key] == expected[key], 'all candidates/aliases and pool order retained')
        keys, inputs, tails = r['candidate_keys'], r['candidate_input_sha256s'], r['candidate_token_ids']
        need(type(keys) is list and type(inputs) is list and type(tails) is list and
             1 <= len(keys) <= 132 and len(keys) == len(inputs) == len(tails) and all(sha(x) for x in keys + inputs), 'full candidate mapping')
        context = pack(r['context_token_ids'], True); candidates = []
        for key, inp, ids in zip(keys, inputs, tails):
            candidate = pack(ids, False)
            need(len(r['context_token_ids']) + len(ids) <= 64, 'no clipping combined sequence')
            identity = (inp, tuple(r['context_token_ids']), tuple(ids))
            if key in seen:
                need(seen[key] == identity, 'shared cache key exact token/input identity')
            seen[key] = identity
            token_identity = (tuple(r['context_token_ids']), tuple(ids))
            if inp in seen_inputs:
                need(seen_inputs[inp] == token_identity, 'same input identity must have identical tokens')
            seen_inputs[inp] = token_identity
            candidates.append(candidate)
        result.append({'id': r['id'], 'context': context, 'candidates': candidates})
    return {'status': 'PENDING_INDEPENDENT_MATERIALIZATION_REVIEW', 'namespace': expected_namespace,
            'embedding_binding_sha256': descriptor_seal, 'frozen_embeddings_sha256': descriptor['tensor_sha256'],
            'tokenization_sha256': tokenization_seal, 'records': result,
            'packed_record_manifest_sha256': digest(result), 'student_update_authorized': False}
