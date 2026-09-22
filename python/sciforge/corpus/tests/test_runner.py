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
    SchemaError,
)
from sciforge.corpus.runner import (
    load_cases_dat,
    load_cases_rust_toml,
    load_cases_toml,
    re_like_engine,
    run_corpus,
)


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

    def test_rust_toml_adapter(self):
        # The rust-regex shape: [[test]] with regex/haystack/matches (a list of matches, each a group-0
        # span or a list of group spans). anchored / bounds / non-utf8 tests are filtered and counted.
        doc = "\n".join([
            '[[test]]', 'name = "a"', "regex = 'a(b)'", 'haystack = "ab"',
            'matches = [[[0, 2], [1, 2]]]', 'unicode = false',
            '',
            '[[test]]', 'name = "b"', "regex = 'a'", 'haystack = "aa"',
            'matches = [[0, 1], [1, 2]]',
            '',
            '[[test]]', 'name = "skip"', "regex = 'a'", 'haystack = "a"',
            'matches = [[0, 1]]', 'anchored = true',   # out-of-API -> filtered
        ])
        with tempfile.NamedTemporaryFile("w", suffix=".toml", delete=False) as handle:
            handle.write(doc)
            path = handle.name
        try:
            cases, filtered = load_cases_rust_toml(path)
            self.assertEqual(filtered, 1)               # the anchored test is filtered
            self.assertEqual(len(cases), 2)
            self.assertEqual(cases[0].flags, ["ascii"])  # unicode=false -> ascii
            self.assertEqual(cases[0].expected, [{"span": [0, 2], "groups": [[1, 2]]}])
            self.assertEqual(cases[1].expected,
                             [{"span": [0, 1], "groups": []}, {"span": [1, 2], "groups": []}])
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


class TestFlagMapping(unittest.TestCase):
    """The flag mapping must fail loudly, and the witness is the STORED authority.

    The defect this guards: `getattr(module, name.upper(), 0)` compiled `icase` to 0 on every
    module (none has ICASE), so engine and oracle ran case-SENSITIVELY, agreed, and passed --
    while the corpus's stored expectation said otherwise and nothing consulted it.
    """

    def test_icase_reaches_the_engine(self):
        # 'a' on "A" with icase must match [0,1] -- the corpus's stored answer. With the flag
        # dropped, re answers None; asserting the STORED expectation, not engine agreement, is
        # what catches the drop.
        import re
        driver = re_like_engine(re)
        self.assertEqual(driver("a", "A", "search", ["icase"]), {"span": [0, 1], "groups": []})

    def test_unknown_flag_is_a_schema_error(self):
        with self.assertRaises(SchemaError):
            Case(pattern="a", input="x", flags=["i"]).validate()  # short forms are not the vocabulary

    def test_unmapped_flag_raises_at_run(self):
        # Defence in depth: a driver called without validate() still cannot compile a flag to 0.
        import re
        driver = re_like_engine(re)
        with self.assertRaises(KeyError):
            driver("a", "A", "search", ["not-a-flag"])


if __name__ == "__main__":
    unittest.main()
