"""Completeness and determinism of the exhaustive enumerator (mode 3).

The two properties the mode promises: the enumeration is **complete** (the input count matches its
closed form) and **deterministic** (two runs are byte-identical, so a gated diff is stable and two JSON
reports compare equal). Pattern counts at small k are pinned as regression anchors.
"""

import unittest

from sciforge.corpus.exhaustive import (
    count_inputs,
    enumerate_inputs,
    enumerate_patterns,
    enumerate_cases,
)


class TestCompleteness(unittest.TestCase):
    def test_input_count_matches_closed_form(self):
        # Strings of length <= n over {a,b} number 2^(n+1)-1; each also appears with one trailing
        # out-of-alphabet char (distinct, since base strings never end in it), doubling the total.
        for n in range(0, 9):
            self.assertEqual(len(enumerate_inputs(n)), count_inputs(n))

    def test_inputs_include_the_empty_string_and_an_out_of_alphabet_case(self):
        inputs = enumerate_inputs(2)
        self.assertIn("", inputs)
        self.assertIn("c", inputs)       # the out-of-alphabet character alone
        self.assertIn("ac", inputs)      # and appended
        self.assertNotIn("cc", inputs)   # only ONE out-of-alphabet char is added


class TestDeterminism(unittest.TestCase):
    def test_patterns_are_stable_and_sorted(self):
        for k in range(1, 5):
            first = enumerate_patterns(k)
            self.assertEqual(first, enumerate_patterns(k))   # identical across runs
            self.assertEqual(first, sorted(first))           # sorted -> reproducible order
            self.assertEqual(len(first), len(set(first)))    # de-duplicated

    def test_pattern_counts_are_pinned(self):
        # Regression anchors for the tier-1 grammar (a change here is a change to the enumerated space).
        self.assertEqual(len(enumerate_patterns(2)), 78)
        self.assertEqual(len(enumerate_patterns(3)), 870)

    def test_cases_are_the_full_cross_product(self):
        npat = len(enumerate_patterns(2))
        ninp = len(enumerate_inputs(3))
        self.assertEqual(sum(1 for _ in enumerate_cases(2, 3)), npat * ninp)


if __name__ == "__main__":
    unittest.main()
