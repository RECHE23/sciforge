"""The runner's self-test: a mini internal corpus where each of the five statuses is exercised at least
once, driven by stub engine/oracle functions. The runner proves it classifies end-to-end before any
real corpus is loaded. Plus the TOML and Fowler-.dat loaders round-trip.
"""

import os
import tempfile
import unittest

from sciforge.corpus import (
    BUG,
    EXCLUDED_BY_DESIGN,
    INTENTIONAL_DIVERGENCE,
    OUT_OF_CONTRACT,
    PASS,
    Case,
    Manifest,
)
from sciforge.corpus.runner import load_cases_dat, load_cases_toml, re_like_engine, run_corpus


def _stub(table):
    """An engine/oracle driver that looks up a pre-programmed observable by pattern."""
    return lambda pattern, text, api, flags: table[pattern]


def _manifest(**over):
    base = dict(origin="mini", sha256="x", license="MIT", attribution="a", retrieved="2026-07-03",
                semantics="leftmost-first", type="text", api="search", oracle="python_re")
    base.update(over)
    return Manifest(**base).validate()


class TestFiveStatusSelfTest(unittest.TestCase):
    def test_every_status_is_reached(self):
        # Corpus A (leftmost-first): pass, bug, excluded_by_design, intentional_divergence.
        cases_a = [
            Case(pattern="pass", input="x", expected={"span": [0, 1]}),
            Case(pattern="bug", input="x", expected={"span": [0, 1]}),
            Case(pattern="backref", input="x", expected={"span": [0, 2]}, requires="backreference"),
            Case(pattern="ext", input="x", expected={"span": [0, 3]},
                 status_expected={"status": INTENTIONAL_DIVERGENCE, "reason": "\\< extension",
                                  "link": "div_wordedge"}),
        ]
        engine_a = _stub({"pass": {"span": [0, 1]}, "bug": {"span": [0, 9]},
                          "backref": None, "ext": {"span": [0, 3]}})
        oracle_a = _stub({"pass": {"span": [0, 1]}, "bug": {"span": [0, 1]},
                          "backref": {"span": [0, 2]}, "ext": "error"})  # re rejects the extension
        report_a = run_corpus(cases_a, _manifest(), engine=engine_a, oracle=oracle_a)
        counts_a = report_a.counts()
        self.assertEqual(counts_a[PASS], 1)
        self.assertEqual(counts_a[BUG], 1)
        self.assertEqual(counts_a[EXCLUDED_BY_DESIGN], 1)
        self.assertEqual(counts_a[INTENTIONAL_DIVERGENCE], 1)

        # Corpus B (leftmost-longest origin, leftmost-first oracle): out_of_contract.
        cases_b = [Case(pattern="a|ab", input="ab", expected={"span": [0, 2]})]  # RE2 stored "ab"
        engine_b = _stub({"a|ab": {"span": [0, 1]}})   # real: leftmost-first "a"
        oracle_b = _stub({"a|ab": {"span": [0, 1]}})   # re:   leftmost-first "a"
        report_b = run_corpus(cases_b, _manifest(semantics="leftmost-longest"),
                              engine=engine_b, oracle=oracle_b)
        self.assertEqual(report_b.counts()[OUT_OF_CONTRACT], 1)

        # All five statuses were reached across the two mini-corpora.
        reached = set()
        for report in (report_a, report_b):
            reached.update(r.status for r in report.results)
        self.assertEqual(reached, {PASS, BUG, EXCLUDED_BY_DESIGN, INTENTIONAL_DIVERGENCE, OUT_OF_CONTRACT})

    def test_real_only_uses_stored_expected_as_oracle(self):
        # With oracle == real_only the stored expectation IS the authority; a match passes, a miss bugs.
        cases = [Case(pattern="ok", input="x", expected={"span": [0, 1]}),
                 Case(pattern="ko", input="x", expected={"span": [0, 1]})]
        engine = _stub({"ok": {"span": [0, 1]}, "ko": None})
        report = run_corpus(cases, _manifest(oracle="real_only"), engine=engine, oracle=None)
        self.assertEqual(report.counts()[PASS], 1)
        self.assertEqual(report.counts()[BUG], 1)


class TestLoaders(unittest.TestCase):
    def test_toml_round_trip(self):
        doc = (
            '[manifest]\n'
            'origin = "u"\nsha256 = "s"\nlicense = "MIT"\nattribution = "a"\n'
            'retrieved = "2026-07-03"\nsemantics = "leftmost-first"\ntype = "text"\n'
            'api = "search"\noracle = "python_re"\n\n'
            '[[case]]\npattern = "a+"\ninput = "aaa"\nflags = ["icase"]\n'
            'expected = { span = [0, 3] }\n'
        )
        with tempfile.NamedTemporaryFile("w", suffix=".toml", delete=False) as handle:
            handle.write(doc)
            path = handle.name
        try:
            manifest, cases = load_cases_toml(path)
            self.assertEqual(manifest.semantics, "leftmost-first")
            self.assertEqual(len(cases), 1)
            self.assertEqual(cases[0].pattern, "a+")
            self.assertEqual(cases[0].flags, ["icase"])
        finally:
            os.unlink(path)

    def test_dat_adapter(self):
        data = "\n".join([
            "# a comment",
            "NOTE something",
            "\ta+\taaa\t(0,3)",       # a match with a whole-match span
            "\tb+\taaa\tNOMATCH",     # a no-match sentinel
        ])
        with tempfile.NamedTemporaryFile("w", suffix=".dat", delete=False) as handle:
            handle.write(data)
            path = handle.name
        try:
            manifest = _manifest(semantics="posix")
            cases = load_cases_dat(path, manifest)
            self.assertEqual(len(cases), 2)
            self.assertEqual(cases[0].expected["span"], [0, 3])
            self.assertIsNone(cases[1].expected)
        finally:
            os.unlink(path)

    def test_re_like_engine_drives_a_module(self):
        # Any re-compatible module works; use the stdlib re as both engine and oracle -> all pass.
        import re
        cases = [Case(pattern="a(b)c", input="abc", expected=None)]
        driver = re_like_engine(re)
        report = run_corpus(cases, _manifest(), engine=driver, oracle=driver)
        self.assertEqual(report.counts()[PASS], 1)
        # the observable carries the group span too
        self.assertEqual(report.results[0].real_result["groups"], [[1, 2]])


if __name__ == "__main__":
    unittest.main()
