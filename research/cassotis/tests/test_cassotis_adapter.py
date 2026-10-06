# SPDX-License-Identifier: GPL-3.0-only
"""Model-free contracts for alignment, branch isolation, completeness and cache."""
import math
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from cassotis_onnx_adapter import CassotisScorer, Vocabulary, chunks, pack
from audit_cassotis_assets import fields


class ToyScorer(CassotisScorer):
    def __init__(self):
        self._owner_thread = threading.get_ident()
        self._allow_experimental_cache = True
        self.vocabulary = Vocabulary({ord(c): i + 2 for i, c in enumerate("甲乙丙丁戊己庚辛壬癸😀")}, 256, 16)
        self._busy = threading.Lock()
        self.prefill_count = 0
        self.clear()

    def _run(self, tree, *, past=None, presents=False):
        prefix = [] if past is None else past[0]
        rows = []
        chains = []
        for index in range(len(tree.ids)):
            node, chain = index, []
            while node >= 0:
                chain.append(tree.ids[node])
                node = tree.parents[node]
            chain = [*prefix, *reversed(chain)]
            chains.append(chain)
            # The prediction depends on every ancestor, so sibling leakage and
            # context-token misalignment change the result.
            signature = sum((i + 1) * token for i, token in enumerate(chain))
            logits = np.asarray([math.sin(signature + token * .7) for token in range(16)], dtype=np.float64)
            rows.append((logits - np.log(np.exp(logits).sum())).astype(np.float32))
        outputs = [np.asarray([rows])]
        if presents:
            self.prefill_count += 1
            outputs.append(chains[-1])
        return outputs


class Contracts(unittest.TestCase):
    def test_sibling_branches_cannot_attend_to_each_other(self):
        tree = pack([0, 2], [[3, 4], [3, 5], [6]])
        mask = tree.mask()[0]
        self.assertEqual(tree.positions, [0, 1, 2, 3, 3, 2])
        self.assertEqual(mask[4].tolist(), [1, 1, 1, 0, 1, 0])
        self.assertEqual(mask[5].tolist(), [1, 1, 0, 0, 0, 1])

    def test_cached_mask_has_prefix_and_ancestors_only(self):
        tree = pack([], [[2, 3], [2, 4]], cached_length=9)
        self.assertEqual(tree.positions, [9, 10, 10])
        self.assertEqual(tree.mask(9)[0, 2].tolist(), [1] * 9 + [1, 0, 1])

    def test_every_candidate_is_scored_across_node_batches(self):
        candidates = [[index + 2] * 200 for index in range(11)]
        batches = list(chunks([0], candidates))
        self.assertEqual([begin for begin, _ in batches], [0, 5, 10])
        self.assertEqual(sum(len(tree.paths) for _, tree in batches), 11)
        self.assertTrue(all(len(tree.ids) <= 1024 for _, tree in batches))

    def test_first_character_is_conditioned_on_bos_or_last_context(self):
        model = ToyScorer()
        for context in ("", "乙丙"):
            prefix, _ = model._prepare(context, ["甲"])
            direct = model._run(pack(prefix, []))[0][0, -1, 2]
            self.assertAlmostEqual(model.score_sums(context, ["甲"])[0], direct, places=6)

    def test_packed_scores_match_independent_sequences(self):
        model = ToyScorer()
        texts = ["甲乙", "甲丙", "乙丁", "甲", "😀甲"]
        packed = model.score_sums("丙丁", texts)
        separate = [model.score_sums("丙丁", [text])[0] for text in texts]
        np.testing.assert_allclose(packed, separate, atol=1e-6, rtol=0)

    def test_independent_path_fixes_shared_context_and_preserves_source_order(self):
        model = ToyScorer()
        texts = ["甲", "甲乙丙丁" * 30, "乙丙"]
        forward = model.score_independent("丁" * 300, texts)
        reversed_scores = model.score_independent("丁" * 300, list(reversed(texts)))
        np.testing.assert_allclose(forward, list(reversed(reversed_scores)), atol=1e-6, rtol=0)
        self.assertIsNone(model._past)

    def test_cache_equivalence_after_edits_context_and_scope_changes(self):
        model = ToyScorer()
        for context, texts, scope in [("", ["甲", "甲乙", "乙丙"], "A"),
                                       ("", ["甲丙", "甲", "乙"], "A"),
                                       ("丁戊", ["甲乙", "甲丙"], "A"),
                                       ("丁戊", ["甲丙", "甲乙"], "B"),
                                       ("", ["乙", "甲乙"], "A")]:
            # Use an independent session reference so it cannot clear the tested cache.
            full = ToyScorer().score_full(context, texts)
            cached = model.score_cached(context, texts, scope=scope)
            np.testing.assert_allclose(cached, full, atol=1e-6, rtol=0)
        self.assertEqual(model.prefill_count, 4)

    def test_context_room_is_same_for_entire_pool(self):
        prefix, tokens = ToyScorer()._prepare("甲" * 400, ["乙", "丙" * 200])
        self.assertEqual(len(prefix), 56)
        self.assertEqual(list(map(len, tokens)), [1, 200])

    def test_complete_candidate_is_never_shortened(self):
        model = ToyScorer()
        with self.assertRaises(ValueError):
            model.score_sums("", ["甲" * 256])
        self.assertIsNone(model._past)

    def test_oov_and_supplementary_scalar_alignment(self):
        model = ToyScorer()
        self.assertEqual(model.encode("😀?"), [12, 1])
        self.assertEqual(len(model.encode("😀")), 1)
        for invalid in ("\x00", "\ud800"):
            with self.assertRaises((ValueError, UnicodeEncodeError)):
                model.encode(invalid)

    def test_cache_requires_explicit_scope(self):
        for unknown in (None, False, 0, "", (), {}):
            model = ToyScorer()
            model.score_cached("乙丙", ["甲"], scope="owned-A")
            self.assertIsNotNone(model._past)
            with self.assertRaises(ValueError):
                model.score_sums("", ["甲"], cached=True, scope=unknown)
            self.assertIsNone(model._past)
            self.assertIsNone(model._root_logp)
            self.assertIsNone(model._cache_key)

    def test_unqualified_cache_is_disabled_and_clears_prior_context(self):
        model = ToyScorer()
        model.score_cached("乙丙", ["甲"], scope="owned-A")
        model._allow_experimental_cache = False
        with self.assertRaises(RuntimeError):
            model.score_cached("丁", ["甲"], scope="owned-B")
        self.assertIsNone(model._past)

    def test_busy_refusal_clears_previous_field_context(self):
        model = ToyScorer()
        model.score_cached("乙丙", ["甲"], scope="owned-A")
        model._busy.acquire()
        try:
            with self.assertRaises(RuntimeError):
                model.score_cached("丁", ["甲"], scope="owned-B")
            self.assertIsNone(model._past)
            self.assertIsNone(model._root_logp)
        finally:
            model._busy.release()

    def test_foreign_thread_cannot_clear_owner_cache(self):
        model = ToyScorer()
        model.score_cached("乙丙", ["甲"], scope="owned-A")
        failures = []
        def foreign():
            try:
                model.clear()
            except RuntimeError:
                failures.append(True)
        thread = threading.Thread(target=foreign)
        thread.start()
        thread.join()
        self.assertEqual(failures, [True])
        self.assertIsNotNone(model._past)
        model.clear()
        self.assertIsNone(model._past)

    def test_over_budget_context_clears_cache_before_encoding(self):
        model = ToyScorer()
        model.score_cached("乙丙", ["甲"], scope="owned-A")
        with self.assertRaises(ValueError):
            model.score_cached("甲" * 4097, ["乙"], scope="owned-A")
        self.assertIsNone(model._past)

    def test_invalid_vocab_rejected_without_native_code(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad_vocab.bin"
            points = list(range(0x4E00, 0x4E00 + 12015))
            for bad in (points[:-1], [*points[:-1], points[0]], [*points[:-1], 0xD800]):
                path.write_bytes(b"CASSLM01" + struct.pack("<III", 12015, 256, 12017)
                                 + struct.pack("<" + "I" * len(bad), *bad))
                with self.assertRaises(ValueError):
                    Vocabulary.read(path)

    def test_malformed_protobuf_bounds_fail_closed(self):
        for data in (b"\x0a\xff", b"\x0a\x05xx", b"\x00", b"\x0b"):
            with self.assertRaises(ValueError):
                list(fields(data, 0, len(data)))


if __name__ == "__main__":
    unittest.main()
