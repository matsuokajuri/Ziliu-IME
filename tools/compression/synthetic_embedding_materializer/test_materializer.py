import copy, hashlib, struct, unittest
from materializer import materialize, digest

class MaterializerTests(unittest.TestCase):
    def setUp(self):
        self.raw = struct.pack('<8f', 0., -0., 1., 2., 3., 4., 5., 6.)
        self.vocab = ['BOS', 'UNK', 'a', 'b']
        self.d = dict(schema='FROZEN_E_BINDING_V1', namespace='SYNTHETIC_QUALIFICATION_V1', endpoint_sha256='a'*64,
                      tensor_name='tok.weight', tensor_sha256=hashlib.sha256(self.raw).hexdigest(), shape=[4,2],
                      dtype='<f4', layout='C', vocabulary_sha256=digest(self.vocab), tokenizer_source_sha256='b'*64,
                      bos_id=0, unk_id=1)
        self.order = [dict(id=str(i), candidate_keys=['c'*64, 'c'*64], candidate_input_sha256s=['d'*64,'d'*64]) for i in range(4)]
        self.t = dict(schema='COMPLETE_FOUR_POOL_TOKENIZATION_V1', namespace=self.d['namespace'], split='TRAIN',
                      binding_sha256=digest(self.d), source_manifest_sha256='e'*64, final_exclusion_policy_sha256='f'*64,
                      records=[dict(**r, context_token_ids=[0,2], candidate_token_ids=[[3],[3]]) for r in self.order])
    def run_it(self, ds=None, ts=None, namespace=None):
        return materialize(self.raw,self.vocab,self.d,ds or digest(self.d),self.t,ts or digest(self.t),namespace or self.d['namespace'],self.order)
    def rebind(self): self.t['binding_sha256']=digest(self.d)
    def test_exact_conversion_aliases_signed_zero(self):
        r=self.run_it();self.assertFalse(r['student_update_authorized']);self.assertEqual(len(r['records']),4)
        for x in r['records']:
            self.assertEqual(len(x['candidates']),2);self.assertEqual(x['candidates'][0],x['candidates'][1])
            self.assertEqual(bytes.fromhex(x['context']['hex']),struct.pack('<4d',0.,-0.,3.,4.))
        self.assertEqual(r['packed_record_manifest_sha256'],digest(r['records']))
    def test_context_unk_preserved_exactly(self):
        for record in self.t['records']:
            record['context_token_ids'] = [0, 1, 2]
        r = self.run_it()
        for record in r['records']:
            self.assertEqual(record['context']['shape'], [3, 2])
            self.assertEqual(bytes.fromhex(record['context']['hex']), struct.pack('<6d', 0., -0., 1., 2., 3., 4.))
            self.assertEqual(len(record['candidates']), 2)
        self.assertFalse(r['student_update_authorized'])
    def test_context_unk_does_not_allow_tail_unk(self):
        for record in self.t['records']:
            record['context_token_ids'] = [0, 1, 2]
            record['candidate_token_ids'] = [[1], [1]]
        with self.assertRaises(ValueError): self.run_it()
    def test_context_bool_rejected(self):
        for record in self.t['records']: record['context_token_ids'] = [0, True]
        with self.assertRaises(ValueError): self.run_it()
    def test_context_out_of_range_rejected(self):
        for record in self.t['records']: record['context_token_ids'] = [0, 4]
        with self.assertRaises(ValueError): self.run_it()
    def test_context_unk_does_not_replace_bos(self):
        for record in self.t['records']: record['context_token_ids'] = [1, 2]
        with self.assertRaises(ValueError): self.run_it()
    def test_input_not_mutated(self):
        before=copy.deepcopy((self.d,self.t,self.order,self.vocab));self.run_it();self.assertEqual(before,(self.d,self.t,self.order,self.vocab))
    def test_wrong_bytes(self):
        self.raw=self.raw[:-1]+b'x'
        with self.assertRaises(ValueError):self.run_it()
    def test_zero_provenance_rejected(self):
        self.d['tokenizer_source_sha256']='0'*64;self.rebind()
        with self.assertRaises(ValueError):self.run_it()
    def test_bad_descriptor_seal(self):
        with self.assertRaises(ValueError):self.run_it(ds='0'*64)
    def test_bad_tokenization_seal(self):
        with self.assertRaises(ValueError):self.run_it(ts='0'*64)
    def test_boolean_dimension(self):
        self.d['shape']=[True,2];self.rebind()
        with self.assertRaises(ValueError):self.run_it()
    def test_reordered_vocab(self):
        self.vocab.reverse()
        with self.assertRaises(ValueError):self.run_it()
    def test_nonfinite_even_unused_row(self):
        self.raw=struct.pack('<8f',0,0,float('inf'),0,3,4,5,6)
        self.d['tensor_sha256']=hashlib.sha256(self.raw).hexdigest();self.rebind()
        with self.assertRaises(ValueError):self.run_it()
    def test_real_namespace_cannot_relabel_fixture(self):
        self.d['namespace']='REAL_DATA_FORBIDDEN';self.t['namespace']='REAL_DATA_FORBIDDEN';self.rebind()
        with self.assertRaises(ValueError):self.run_it()
    def test_namespace_external(self):
        with self.assertRaises(ValueError):self.run_it(namespace='REAL_DATA_FORBIDDEN')
    def test_final_and_dev_forbidden(self):
        for split in ('FINAL','DEV'):
            self.t['split']=split
            with self.assertRaises(ValueError):self.run_it()
    def test_no_candidate_drop(self):
        self.t['records'][0]['candidate_token_ids'].pop()
        with self.assertRaises(ValueError):self.run_it()
    def test_no_order_substitution(self):
        self.t['records'].reverse()
        with self.assertRaises(ValueError):self.run_it()
    def test_unknown_not_filtered(self):
        self.t['records'][0]['candidate_token_ids'][0]=[1]
        with self.assertRaises(ValueError):self.run_it()
    def test_bool_token_rejected(self):
        self.t['records'][0]['candidate_token_ids'][0]=[True]
        with self.assertRaises(ValueError):self.run_it()
    def test_out_of_range_token(self):
        self.t['records'][0]['candidate_token_ids'][0]=[4]
        with self.assertRaises(ValueError):self.run_it()
    def test_missing_bos(self):
        self.t['records'][0]['context_token_ids']=[2]
        with self.assertRaises(ValueError):self.run_it()
    def test_conflicting_alias(self):
        self.t['records'][0]['candidate_token_ids'][1]=[2]
        with self.assertRaises(ValueError):self.run_it()
    def test_same_input_different_key_cannot_change_tokens(self):
        self.order[0]['candidate_keys'][1]='1'*64
        self.t['records'][0]['candidate_keys']=self.order[0]['candidate_keys'][:]
        self.t['records'][0]['candidate_token_ids'][1]=[2]
        with self.assertRaises(ValueError):self.run_it()
    def test_combined_length(self):
        self.t['records'][0]['context_token_ids']=[0]+[2]*63
        with self.assertRaises(ValueError):self.run_it()
    def test_extra_field(self):
        self.t['records'][0]['gold_index']=0
        with self.assertRaises(ValueError):self.run_it()

if __name__=='__main__':unittest.main()
