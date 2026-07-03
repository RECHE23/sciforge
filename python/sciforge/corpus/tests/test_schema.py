"""The corpus contract, exercised — and its adversarial acceptance test.

The heart is :func:`test_ten_trap_cases`: ten deliberately awkward cases classified by hand against the
written rules. If any one of them were ambiguous under :func:`sciforge.corpus.classify` — resolvable to
more than one status, or to a surprising one — the contract would be under-specified and must go back to
revision before any real corpus is imported. They all resolve to exactly one status here.
"""

import unittest

from sciforge.corpus import (
    BUG,
    EXCLUDED_BY_DESIGN,
    INTENTIONAL_DIVERGENCE,
    OUT_OF_CONTRACT,
    PASS,
    Case,
    CaseResult,
    CorpusReport,
    Manifest,
    SchemaError,
    classify,
    is_empty_iteration_capture,
    report_from_json,
    report_to_json,
)


def _manifest(**over):
    base = dict(origin="https://example/corpus.toml", sha256="deadbeef", license="MIT",
                attribution="Author", retrieved="2026-07-03", semantics="leftmost-first",
                type="text", api="search", oracle="python_re")
    base.update(over)
    return Manifest(**base)


class TestTrapCases(unittest.TestCase):
    def test_ten_trap_cases(self):
        # Each tuple: a label, the classify(...) kwargs, and the ONE status the rules must yield.
        traps = [
            # 1. A plain agreement with the authority.
            ("plain match",
             dict(real_result=(0, 3), oracle_result=(0, 3), corpus_expected=(0, 3),
                  semantics="leftmost-first", oracle="python_re"), PASS),
            # 2. A genuine miss: real disagrees with re, same semantics, nothing to excuse it.
            ("real gives the wrong span",
             dict(real_result=(0, 1), oracle_result=(0, 3), corpus_expected=(0, 3),
                  semantics="leftmost-first", oracle="python_re"), BUG),
            # 3. A backreference: excluded by the linearity thesis, never a failure.
            ("(a)\\1 backreference",
             dict(real_result=None, oracle_result=(0, 2), corpus_expected=(0, 2),
                  semantics="leftmost-first", oracle="python_re", requires="backreference"),
             EXCLUDED_BY_DESIGN),
            # 4. Recursion: likewise excluded.
            ("recursive pattern",
             dict(real_result=None, oracle_result=(0, 4), corpus_expected=(0, 4),
                  semantics="leftmost-first", oracle="python_re", requires="recursion"),
             EXCLUDED_BY_DESIGN),
            # 5. The RE2 example: `a|ab` on "ab". A leftmost-longest corpus expects "ab"; our leftmost-
            #    first oracle (and real) give "a". The corpus row is under foreign semantics.
            ("a|ab under leftmost-longest",
             dict(real_result=(0, 1), oracle_result=(0, 1), corpus_expected=(0, 2),
                  semantics="leftmost-longest", oracle="python_re"), OUT_OF_CONTRACT),
            # 6. out_of_contract pre-empts a pass: a POSIX corpus row that disagrees with our oracle is
            #    set aside even when real happens to match the oracle -- the row is not ours to score.
            ("posix row real happens to match oracle on",
             dict(real_result=(0, 1), oracle_result=(0, 1), corpus_expected=(0, 3),
                  semantics="posix", oracle="python_re"), OUT_OF_CONTRACT),
            # 7. `\<`/`\>` word-edge anchors: a REAL extension re does not have. re rejects, real
            #    matches; the difference is documented, so it is an intentional divergence.
            ("\\<word\\> extension",
             dict(real_result=(0, 4), oracle_result="reject", corpus_expected=(0, 4),
                  semantics="leftmost-first", oracle="python_re",
                  divergence_link="div_wordedge"), INTENTIONAL_DIVERGENCE),
            # 8. Variable-width lookbehind: re/PCRE reject `(?<=a|bb)`, real accepts it (bounded, linear).
            #    Documented -> intentional divergence, not a bug.
            ("(?<=a|bb) variable-width lookbehind",
             dict(real_result=(1, 2), oracle_result="reject", corpus_expected=(1, 2),
                  semantics="leftmost-first", oracle="python_re",
                  divergence_link="div_lookbehind"), INTENTIONAL_DIVERGENCE),
            # 9. A bytes corpus row that agrees with the oracle.
            ("bytes match",
             dict(real_result=(0, 2), oracle_result=(0, 2), corpus_expected=(0, 2),
                  semantics="leftmost-first", oracle="std_regex"), PASS),
            # 10. A bytes corpus row where real disagrees -- a real bug (nothing to excuse it).
            ("bytes mismatch",
             dict(real_result=None, oracle_result=(0, 2), corpus_expected=(0, 2),
                  semantics="leftmost-first", oracle="std_regex"), BUG),
        ]
        for label, kwargs, want in traps:
            with self.subTest(case=label):
                self.assertEqual(classify(**kwargs), want)

    def test_out_of_contract_only_on_disagreeing_rows(self):
        # A foreign-semantics corpus still scores the rows where it AGREES with our oracle: those are
        # not out_of_contract, so a real disagreement there is a real bug.
        self.assertEqual(
            classify(real_result=(0, 1), oracle_result=(0, 3), corpus_expected=(0, 3),
                     semantics="leftmost-longest", oracle="python_re"),
            BUG)  # corpus_expected == oracle_result, so this row IS in contract -> a real miss is a bug

    def test_real_only_corpus_never_out_of_contract(self):
        # real_only: the corpus IS real's pinned behaviour, so semantics match by definition.
        self.assertEqual(
            classify(real_result="x", oracle_result="x", corpus_expected="x",
                     semantics="leftmost-first", oracle="real_only"), PASS)


class TestEmptyIterationSignature(unittest.TestCase):
    def test_recognises_the_class(self):
        # (a*)* on "a": same match span, group 1 differs, oracle's group is the zero-width empty step.
        real = {"span": [0, 1], "groups": [[0, 1]]}
        oracle = {"span": [0, 1], "groups": [[1, 1]]}
        self.assertTrue(is_empty_iteration_capture(real, oracle))
        # ()* on "": oracle captures the empty (0,0), real leaves it unset.
        self.assertTrue(is_empty_iteration_capture({"span": [0, 0], "groups": [[-1, -1]]},
                                                   {"span": [0, 0], "groups": [[0, 0]]}))

    def test_rejects_a_span_difference(self):
        # A different match span is a genuine selection difference (an a|ab-class bug), never this class.
        self.assertFalse(is_empty_iteration_capture({"span": [0, 1], "groups": []},
                                                    {"span": [0, 2], "groups": []}))

    def test_rejects_a_non_empty_group_difference(self):
        # If the oracle's differing group is NOT zero-width, it is some other divergence, not this one.
        self.assertFalse(is_empty_iteration_capture({"span": [0, 3], "groups": [[0, 1]]},
                                                    {"span": [0, 3], "groups": [[0, 2]]}))

    def test_finditer_sequence(self):
        real = [{"span": [1, 1], "groups": [[-1, -1]]}]
        oracle = [{"span": [1, 1], "groups": [[1, 1]]}]
        self.assertTrue(is_empty_iteration_capture(real, oracle))
        # a length mismatch (different number of matches) is not this class
        self.assertFalse(is_empty_iteration_capture(real, oracle + oracle))


class TestValidation(unittest.TestCase):
    def test_manifest_rejects_unknown_vocabulary(self):
        with self.assertRaises(SchemaError):
            _manifest(semantics="whatever").validate()
        with self.assertRaises(SchemaError):
            _manifest(oracle="grep").validate()
        with self.assertRaises(SchemaError):
            _manifest(origin="").validate()
        _manifest().validate()  # a good one does not raise

    def test_case_override_needs_reason_and_link(self):
        Case("a", "a").validate()  # plain case is fine
        with self.assertRaises(SchemaError):
            Case("a", "a", requires="time-travel").validate()
        with self.assertRaises(SchemaError):
            Case("a", "a", status_expected={"status": INTENTIONAL_DIVERGENCE, "reason": "x"}).validate()
        # a divergence override needs a link; a bug override just needs a reason
        Case("a", "a", status_expected={"status": INTENTIONAL_DIVERGENCE, "reason": "x",
                                        "link": "div_x"}).validate()
        Case("a", "a", status_expected={"status": BUG, "reason": "known"}).validate()


class TestReport(unittest.TestCase):
    def test_counts_cover_all_five_statuses(self):
        report = CorpusReport(corpus="c", manifest=_manifest(), results=[
            CaseResult(status=PASS, pattern="a"),
            CaseResult(status=PASS, pattern="b"),
            CaseResult(status=BUG, pattern="c"),
            CaseResult(status=EXCLUDED_BY_DESIGN, pattern="d"),
        ])
        counts = report.counts()
        self.assertEqual(counts[PASS], 2)
        self.assertEqual(counts[BUG], 1)
        self.assertEqual(counts[EXCLUDED_BY_DESIGN], 1)
        self.assertEqual(counts[OUT_OF_CONTRACT], 0)  # zero-filled, present in every report

    def test_report_json_round_trip(self):
        report = CorpusReport(corpus="c", mode="exhaustive", manifest=_manifest(),
                              results=[CaseResult(status=PASS, pattern="a", real_result=(0, 1))])
        back = report_from_json(report_to_json(report))
        self.assertEqual(back.corpus, "c")
        self.assertEqual(back.mode, "exhaustive")
        self.assertEqual(back.manifest.semantics, "leftmost-first")
        self.assertEqual(back.results[0].status, PASS)
        self.assertEqual(back.counts()[PASS], 1)


if __name__ == "__main__":
    unittest.main()
