"""Native fact boundaries with synthetic evidence, including mixed text lengths."""
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import offline_candidate_evidence as E


class EvidenceTests(unittest.TestCase):
    def observation(self, text="one", kind="phrase", dynamic="Phrase", tokens=None):
        identity = {"nonce": "a" * 32, "input": "xian", "source_index": 0, "text": text}
        return dict(identity, genuines=[{"dynamic_type": dynamic, "type": kind,
            "start": 0, "end": 4, "remaining_code_length": 0,
            "canonical_tokens": tokens or ["xian"], "vertices": [0, 4], "text": text}])

    def selection(self, observation):
        return {**{key: observation[key] for key in ("nonce", "input", "source_index", "text")},
            "selected": True, "commit": observation["text"], "remaining_input": "", "remaining_preedit": ""}

    def facts(self, observation, selection=None):
        return E.candidate_facts(observation, selection or self.selection(observation), qualified_observer=True)

    def test_mixed_length_segmentations_use_native_code_and_span(self):
        for text, tokens in (("one", ["xian"]), ("two-characters", ["xi", "an"])):
            observation = self.observation(text=text, tokens=tokens)
            self.assertEqual(self.facts(observation).answers_key, True)
            self.assertEqual(self.facts(observation).trusted_dictionary_hit, True)

    def test_predictive_completion_is_not_whole_even_when_selection_consumes_all(self):
        observation = self.observation(kind="completion")
        observation["genuines"][0]["remaining_code_length"] = 1
        self.assertFalse(self.facts(observation).answers_key)

    def test_partial_spans_and_partial_commits_are_rejected(self):
        observation = self.observation()
        observation["genuines"][0]["end"] = 2
        self.assertFalse(self.facts(observation).answers_key)
        observation = self.observation()
        self.assertFalse(self.facts(observation,
            dict(self.selection(observation), remaining_input="an")).answers_key)

    def test_scope_selection_identity_cannot_be_mixed(self):
        observation = self.observation()
        for key, value in (("nonce", "b" * 32), ("text", "other"), ("source_index", 1), ("input", "other")):
            with self.assertRaises(ValueError):
                self.facts(observation, dict(self.selection(observation), **{key: value}))

    def test_unknowns_and_multi_genuine_never_become_untrusted_false(self):
        observation = self.observation(dynamic="Simple")
        self.assertIsNone(self.facts(observation).trusted_dictionary_hit)
        observation["genuines"][0]["remaining_code_length"] = None
        self.assertIsNone(self.facts(observation).answers_key)
        observation = self.observation()
        observation["genuines"] *= 2
        self.assertIsNone(self.facts(observation).trusted_dictionary_hit)
        observation = self.observation(tokens=["xiong", "an"])
        self.assertIsNone(self.facts(observation).trusted_dictionary_hit)
        unknown = E.candidate_facts(observation, self.selection(observation), qualified_observer=False)
        self.assertIsNone(unknown.answers_key)
        self.assertIsNone(unknown.trusted_dictionary_hit)

    def test_single_native_composed_sentence_is_distinct_from_dictionary_phrase(self):
        observation = self.observation(kind="sentence", dynamic="Sentence")
        self.assertEqual(self.facts(observation).trusted_dictionary_hit, False)

    def test_carrier_is_bound_to_native_candidate_nonce_input_and_source_index(self):
        hx = lambda text: text.encode().hex()
        comment = "original" + E.MARKER + "|".join(["v1", "a" * 32, "0", hx("xian"), hx("one"),
            ",".join([hx("Phrase"), hx("phrase"), "0", "4", "0", hx("xian"), "0:4", hx("xian"), hx("one")])])
        parsed = E.parse_observation(comment, nonce="a" * 32, keys="xian", source_index=0, text="one")
        self.assertEqual(parsed["original_comment"], "original")
        self.assertTrue(self.facts(parsed).trusted_dictionary_hit)
        for changes in ({"nonce": "b" * 32}, {"keys": "xi"}, {"source_index": 1}, {"text": "other"}):
            args = dict(nonce="a" * 32, keys="xian", source_index=0, text="one", **{})
            args.update(changes)
            with self.assertRaises(ValueError):
                E.parse_observation(comment, **args)


if __name__ == "__main__":
    unittest.main()
