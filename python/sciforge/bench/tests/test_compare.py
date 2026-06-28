"""Selftest for the comparison layer — the agnosticism proof, the mini-review in cases.

compare() is exercised through four genuinely different equality callables (exact, count,
tolerance, a context-capturing closure) to show the core carries no domain: nothing here is
== or allclose hardcoded inside compare/verdict; every judgement comes from the callable.
A mismatch case proves a wrong answer is marked and fails the verdict, and synthetic cases
with controlled ratios pin the CI-aware classification (clears / straddles / below 1.0).
"""

import unittest

from sciforge.bench import Case, compare, verdict


def _case(name, ratios, mismatch=False, key=True):
    """A synthetic comparison Case with controlled ratios (no timing), for verdict() tests."""
    n = len(ratios) or 1
    return Case(name=name, unit="s", samples=[1.0] * n,
                extra={"reference_samples": [1.0] * n, "ratios": list(ratios),
                       "mismatch": mismatch, "key": key})


class ComparatorTest(unittest.TestCase):
    """Four different equality callables — the core stays domain-free, the callable decides."""

    def _well_formed_match(self, case):
        self.assertFalse(case.extra["mismatch"])
        self.assertGreater(len(case.samples), 0)              # timed (gate passed)
        self.assertEqual(len(case.extra["ratios"]), len(case.samples))
        self.assertEqual(len(case.extra["reference_samples"]), len(case.samples))

    def test_exact_equality(self):
        case = compare("exact", lambda: 5, lambda: 5, lambda a, b: a == b, samples=4)
        self._well_formed_match(case)

    def test_count_equality(self):
        # Values differ, counts match — proves the callable, not value equality, is the judge.
        case = compare("count", lambda: [1, 2, 3], lambda: [9, 8, 7],
                       lambda a, b: len(a) == len(b), samples=4)
        self._well_formed_match(case)

    def test_tolerance_equality(self):
        # Pure-stdlib tolerance (no numpy): the floats differ but are within tol.
        tol = 1e-6
        case = compare("tol", lambda: 1.0, lambda: 1.0 + 1e-9,
                       lambda a, b: abs(a - b) <= tol, samples=4)
        self._well_formed_match(case)

    def test_closure_captures_context(self):
        # equal captures `ctx` — the future symbolic/differential shape: judgement needs context.
        ctx = {"scale": 2}
        case = compare("closure", lambda: 4, lambda: 2,
                       lambda a, b: a == b * ctx["scale"], samples=4)
        self._well_formed_match(case)                          # 4 == 2 * 2, via the captured ctx

    def test_compare_feeds_verdict(self):
        case = compare("ok", lambda: 1, lambda: 1, lambda a, b: a == b, samples=4)
        result = verdict([case])
        self.assertTrue(result.passed)
        self.assertIn(result.classification, {"faster", "slower", "indecisive"})


class MismatchTest(unittest.TestCase):
    def test_mismatch_marks_case_and_skips_timing(self):
        case = compare("bad", lambda: 1, lambda: 2, lambda a, b: a == b, samples=4)
        self.assertTrue(case.extra["mismatch"])
        self.assertEqual(case.samples, [])                    # a wrong answer is not timed
        self.assertEqual(case.extra["ratios"], [])

    def test_mismatch_fails_the_verdict(self):
        good = _case("good", [2.0] * 6)
        bad = _case("bad", [], mismatch=True)
        result = verdict([good, bad])
        self.assertFalse(result.passed)
        self.assertIn("bad", result.mismatched)
        self.assertIn("mismatch", result.text)


class VerdictTest(unittest.TestCase):
    def test_faster_when_ci_clears_one(self):
        result = verdict([_case("a", [2.0] * 6), _case("b", [3.0] * 6)], subject_label="X")
        self.assertEqual(result.classification, "faster")
        self.assertGreater(result.ci_low, 1.0)
        self.assertTrue(result.passed)
        self.assertIn("X faster", result.text)

    def test_slower_when_ci_below_one(self):
        result = verdict([_case("a", [0.5] * 6), _case("b", [0.4] * 6)])
        self.assertEqual(result.classification, "slower")
        self.assertLess(result.ci_high, 1.0)
        self.assertTrue(result.passed)            # slower is not, by itself, a correctness failure

    def test_indecisive_when_ci_straddles_one(self):
        result = verdict([_case("a", [2.0] * 6), _case("b", [0.5] * 6)])
        self.assertEqual(result.classification, "indecisive")
        self.assertLessEqual(result.ci_low, 1.0)
        self.assertGreaterEqual(result.ci_high, 1.0)

    def test_key_selects_gating_cases(self):
        # The non-key case is slower but excluded; the verdict reflects only the key case.
        cases = [_case("keyed", [2.0] * 6, key=True), _case("ignored", [0.1] * 6, key=False)]
        result = verdict(cases)
        self.assertEqual(result.classification, "faster")

    def test_no_comparable_cases(self):
        result = verdict([_case("bad", [], mismatch=True)])
        self.assertEqual(result.classification, "no-data")
        self.assertFalse(result.passed)


if __name__ == "__main__":
    unittest.main()
