"""The conformance-corpus exchange format: a manifest per vendored file, cases, and a per-status report.

This is the *machine* half of the corpus contract (the normative prose is in ``docs/corpus.md``). It is
dependency-free stdlib Python — the same substrate rule as :mod:`sciforge.bench`. It carries three things:

- the **manifest** metadata a vendored corpus file must declare (provenance, licence, and the
  ``semantics`` field that drives the ``out_of_contract`` verdict),
- the **case** exchange record (pattern, input, expected, flags, and an optional status override), and
- the **classification rules** — :func:`classify` turns (real result, oracle result, corpus expectation)
  into exactly one of the five statuses, so the taxonomy is executable, not just documented.

Storing the rules here keeps every runner (static, random, exhaustive) and every scorecard agreeing on
what "pass" and "bug" mean.
"""

import json
from dataclasses import dataclass, field

# --- the five statuses ------------------------------------------------------------------------------
PASS = "pass"                                       #: real matches the declared oracle.
BUG = "bug"                                         #: real disagrees and nothing below explains it.
INTENTIONAL_DIVERGENCE = "intentional_divergence"   #: a documented, on-purpose difference from the oracle.
OUT_OF_CONTRACT = "out_of_contract"                 #: the origin's semantics differ from the oracle's.
EXCLUDED_BY_DESIGN = "excluded_by_design"           #: the pattern needs a thesis-excluded capability.

STATUSES = (PASS, BUG, INTENTIONAL_DIVERGENCE, OUT_OF_CONTRACT, EXCLUDED_BY_DESIGN)

# --- constrained manifest vocabularies --------------------------------------------------------------
#: The origin tester's match-selection semantics. This is the field that decides `out_of_contract`.
SEMANTICS = ("leftmost-first", "leftmost-longest", "posix", "maximal-munch")
#: The authority a corpus is judged against.
ORACLES = ("python_re", "std_regex", "reference_tokenize", "real_only")
#: The input domain.
CASE_TYPES = ("text", "bytes")

#: The match-selection semantics each oracle itself uses — the yardstick the origin's `semantics` is
#: compared against. python_re and std_regex (ECMAScript) are leftmost-first; the lexer oracle is
#: maximal-munch; real_only means the corpus IS real's own pinned behaviour (leftmost-first).
ORACLE_SEMANTICS = {
    "python_re": "leftmost-first",
    "std_regex": "leftmost-first",
    "reference_tokenize": "maximal-munch",
    "real_only": "leftmost-first",
}

#: Capabilities excluded by the engine's linearity thesis; a case needing one is never "bug"/"failed".
EXCLUDED_CAPABILITIES = ("backreference", "recursion", "callout", "subroutine")

#: The engine-flag vocabulary a case may carry (mapped to module attributes by the runner). A name
#: outside this set is a schema error: an unmapped flag must fail loudly, never compile as 0. A
#: silently dropped flag makes engine and oracle wrong IDENTICALLY, and a real-vs-oracle comparison
#: cannot see that — only the stored expectation could, and it is not consulted on every path.
FLAG_NAMES = ("ascii", "dotall", "icase", "multiline", "verbose")


class SchemaError(ValueError):
    """A manifest or case that violates the corpus contract."""


@dataclass
class Manifest:
    """Provenance and contract metadata for one vendored corpus file.

    ``semantics`` (the origin tester's match-selection rule) is the load-bearing field: when it differs
    from the declared ``oracle``'s own semantics, cases whose stored expectation reflects the origin
    rule are classified ``out_of_contract`` rather than ``bug`` — decided here, per file, not per case.
    """

    origin: str          #: The URL the corpus was retrieved from.
    sha256: str          #: Hex digest of the exact vendored bytes.
    license: str         #: SPDX identifier or licence name.
    attribution: str     #: Required attribution text.
    retrieved: str       #: ISO date (YYYY-MM-DD) of retrieval.
    semantics: str       #: One of :data:`SEMANTICS`.
    type: str            #: One of :data:`CASE_TYPES`.
    api: str             #: The engine entry point exercised (search / fullmatch / finditer / …).
    oracle: str          #: One of :data:`ORACLES`.
    notes: str = ""      #: Free-form notes.

    def validate(self):
        """Return self after checking the constrained fields; raise :class:`SchemaError` otherwise."""
        for name, value, allowed in (("semantics", self.semantics, SEMANTICS),
                                     ("type", self.type, CASE_TYPES),
                                     ("oracle", self.oracle, ORACLES)):
            if value not in allowed:
                raise SchemaError("manifest {} {!r} not in {}".format(name, value, allowed))
        for name in ("origin", "sha256", "license", "attribution", "retrieved", "api"):
            if not getattr(self, name):
                raise SchemaError("manifest field {!r} is required".format(name))
        return self


@dataclass
class Case:
    """One corpus case: the pattern, the input, and the expectation, plus optional flags/override.

    ``expected`` is an opaque structured value the runner interprets against the corpus type — a match
    (spans + groups), ``None`` for no-match, or a token list for a lexer corpus. ``requires`` names an
    excluded capability the pattern needs (or is empty). ``status_expected`` is an explicit override
    with a mandatory ``reason`` and, for ``intentional_divergence``, a ``link`` into divergences.dox.
    """

    pattern: str
    input: object                              #: str or bytes (per the manifest ``type``).
    expected: object = None                    #: spans+groups / token list / None.
    flags: list = field(default_factory=list)  #: engine flag names, e.g. ["icase", "multiline"].
    requires: str = ""                         #: an :data:`EXCLUDED_CAPABILITIES` name, or "".
    status_expected: object = None             #: {"status": …, "reason": …, "link": …} or None.

    def validate(self):
        """Return self after checking `requires` and any `status_expected` override."""
        for name in self.flags:
            if name not in FLAG_NAMES:
                raise SchemaError("case flag {!r} not in {}".format(name, FLAG_NAMES))
        if self.requires and self.requires not in EXCLUDED_CAPABILITIES:
            raise SchemaError("case requires {!r} not in {}".format(self.requires, EXCLUDED_CAPABILITIES))
        override = self.status_expected
        if override is not None:
            status = override.get("status")
            if status not in STATUSES:
                raise SchemaError("status_expected.status {!r} not in {}".format(status, STATUSES))
            if not override.get("reason"):
                raise SchemaError("status_expected needs a reason")
            if status == INTENTIONAL_DIVERGENCE and not override.get("link"):
                raise SchemaError("an intentional_divergence override needs a divergences.dox link")
        return self


def classify(real_result,
             oracle_result,
             corpus_expected,
             *,
             semantics,
             oracle,
             requires="",
             divergence_link=None):
    """Return exactly one status for a case, applying the taxonomy's decision rules in order.

    The checks that can pre-empt a plain pass/bug are evaluated first; ``bug`` is only reached when
    real disagrees with the oracle and none of them apply (the contract's definition of a bug).

    Args:
        real_result: What the engine produced (already normalised for comparison).
        oracle_result: What the declared oracle produced for the same case.
        corpus_expected: The expectation stored in the corpus (may be under the origin's semantics).
        semantics: The origin tester's semantics (one of :data:`SEMANTICS`).
        oracle: The declared oracle (one of :data:`ORACLES`).
        requires: An excluded capability the pattern needs, or ``""``.
        divergence_link: A divergences.dox anchor if the difference is documented, else ``None``.

    Returns:
        str: one of :data:`STATUSES`.
    """
    # 1. A pattern that needs an excluded capability is counted apart, never as a failure.
    if requires:
        return EXCLUDED_BY_DESIGN
    # 2. out_of_contract is decided by the manifest, not case-by-case: when the origin's semantics
    #    differ from the oracle's own, a case whose stored expectation reflects the origin rule (it
    #    disagrees with what the oracle produces) is outside this corpus's contract with our oracle.
    if (semantics != ORACLE_SEMANTICS.get(oracle)
            and corpus_expected is not None
            and corpus_expected != oracle_result):
        return OUT_OF_CONTRACT
    # 3. The engine agrees with the authority.
    if real_result == oracle_result:
        return PASS
    # 4. The disagreement is documented and on purpose.
    if divergence_link:
        return INTENTIONAL_DIVERGENCE
    # 5. A real disagreement with nothing above to explain it.
    return BUG


def is_empty_iteration_capture(real_result, oracle_result):
    """True when real and the oracle agree on every match **span** but differ only by the empty-final-
    iteration capture rule: at each differing group the oracle's capture is a **zero-width** span (the
    empty iteration it took and real did not). This is the signature of the documented nullable-loop
    divergence; a consumer maps it to that section's link. A difference in any match span (a real
    selection difference) is *not* this class and returns False.
    """
    def one(real_match, oracle_match):
        if not isinstance(real_match, dict) or not isinstance(oracle_match, dict):
            return False  # None / "error": only this class when both are matches
        if real_match["span"] != oracle_match["span"]:
            return False  # a match-span difference is a genuine selection difference, not this class
        real_groups, oracle_groups = real_match["groups"], oracle_match["groups"]
        if len(real_groups) != len(oracle_groups):
            return False
        differ = False
        for real_span, oracle_span in zip(real_groups, oracle_groups):
            if real_span != oracle_span:
                differ = True
                if oracle_span[0] != oracle_span[1]:  # the oracle's differing group must be zero-width
                    return False
        return differ

    if isinstance(real_result, list) or isinstance(oracle_result, list):
        if not (isinstance(real_result, list) and isinstance(oracle_result, list)):
            return False
        if len(real_result) != len(oracle_result):
            return False
        differ = False
        for real_match, oracle_match in zip(real_result, oracle_result):
            if real_match == oracle_match:
                continue
            if not one(real_match, oracle_match):
                return False
            differ = True
        return differ
    return one(real_result, oracle_result)


@dataclass
class CaseResult:
    """The outcome of running one case: its status plus the values that produced it (for the report)."""

    status: str
    pattern: str
    real_result: object = None
    oracle_result: object = None
    note: str = ""


@dataclass
class CorpusReport:
    """A per-corpus result: the manifest, the per-case results, and the per-status tally.

    The tally is what a scorecard reads; the field shape does not presume how the cases were produced
    (a static list, a random sweep, or an exhaustive enumeration all report identically).
    """

    corpus: str                                #: The corpus file name / id.
    manifest: Manifest
    results: list = field(default_factory=list)
    mode: str = "static"                       #: static | random | exhaustive (provenance-neutral).

    def counts(self):
        """Return a {status: count} dict covering all five statuses (zeros included)."""
        tally = dict.fromkeys(STATUSES, 0)
        for result in self.results:
            tally[result.status] = tally.get(result.status, 0) + 1
        return tally


# --- JSON round-trip (a plain dict, like sciforge.bench) --------------------------------------------

def report_to_json(report):
    """Return the JSON-ready dict form of a :class:`CorpusReport` (counts + per-case results)."""
    return {
        "corpus": report.corpus,
        "mode": report.mode,
        "manifest": vars(report.manifest),
        "counts": report.counts(),
        "results": [vars(result) for result in report.results],
    }


def report_from_json(obj):
    """Rebuild a :class:`CorpusReport` from its dict form (the ``counts`` are recomputed)."""
    return CorpusReport(
        corpus=obj["corpus"],
        mode=obj.get("mode", "static"),
        manifest=Manifest(**obj["manifest"]),
        results=[CaseResult(**result) for result in obj.get("results", [])],
    )


def write_report(report, path):
    """Write `report` to `path` as JSON."""
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report_to_json(report), handle, indent=2)


def load_report(path):
    """Read a :class:`CorpusReport` back from the JSON at `path`."""
    with open(path, "r", encoding="utf-8") as handle:
        return report_from_json(json.load(handle))
