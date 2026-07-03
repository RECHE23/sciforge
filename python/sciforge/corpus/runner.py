"""The reference corpus runner: load a manifest + its cases, run each against real and the oracle,
classify with the QA-0 rules, and emit the per-status report.

Dependency-free stdlib (``tomllib`` needs Python >= 3.11 — a dev-only tool, never shipped; CI runs it on
the 3.14 leg). The runner is **engine/oracle-agnostic**: a consumer injects two drivers with the same
call shape. Because real and ``re`` expose the same surface (``compile`` / ``search`` / ``fullmatch`` /
``finditer``, ``Match.span`` / ``groups``), one factory :func:`re_like_engine` produces both — the
consumer wires ``engine=re_like_engine(real)`` and ``oracle=re_like_engine(re)``.
"""

import tomllib

from .schema import (
    INTENTIONAL_DIVERGENCE,
    Case,
    CaseResult,
    CorpusReport,
    Manifest,
    classify,
)

# --- observables: what a case compares --------------------------------------------------------------
#: A match observable is `None` (no match), `"error"` (the pattern was rejected), or a dict with the
#: match `span` and every group `span`. This is the normalised value real and the oracle are compared on.


def _match_observable(match):
    if match is None:
        return None
    ngroups = len(match.groups())  # both re and real expose groups(); its length is the group count
    return {"span": list(match.span()),
            "groups": [list(match.span(index)) for index in range(1, ngroups + 1)]}


def re_like_engine(module):
    """Return a driver over a re-compatible module (``real`` or ``re``): (pattern, input, api, flags)."""
    def run(pattern, text, api, flags):
        flag_value = 0
        for name in flags:
            flag_value |= getattr(module, name.upper(), 0)
        try:
            compiled = module.compile(pattern, flag_value)
        except module.error:
            return "error"
        if api == "finditer":
            return [_match_observable(m) for m in compiled.finditer(text)]
        matcher = getattr(compiled, api)  # search / fullmatch / match
        return _match_observable(matcher(text))
    return run


# --- running a corpus -------------------------------------------------------------------------------

def _divergence_link(case):
    """The divergences.dox link a case declares, if its override marks an intentional divergence."""
    override = case.status_expected
    if override and override.get("status") == INTENTIONAL_DIVERGENCE:
        return override.get("link")
    return None


def run_corpus(cases, manifest, *, engine, oracle, divergence_of=None):
    """Run every case and return a :class:`CorpusReport`.

    Args:
        cases: an iterable of :class:`Case`.
        manifest: the :class:`Manifest` for this corpus.
        engine: the driver under test — ``(pattern, input, api, flags) -> observable``.
        oracle: the authority driver, same shape. Ignored when ``manifest.oracle == "real_only"``
            (the stored ``expected`` is then the authority).
        divergence_of: an optional ``(real_result, oracle_result, case) -> link | None`` callback that
            recognises a *documented* divergence class and returns its divergences.dox link, so those
            cases classify as ``intentional_divergence`` instead of ``bug``. A per-case ``status_expected``
            override takes precedence over it.

    Returns:
        CorpusReport
    """
    results = []
    for case in cases:
        real_result = engine(case.pattern, case.input, manifest.api, case.flags)
        if manifest.oracle == "real_only":
            oracle_result = case.expected
        else:
            oracle_result = oracle(case.pattern, case.input, manifest.api, case.flags)
        link = _divergence_link(case)
        if link is None and divergence_of is not None:
            link = divergence_of(real_result, oracle_result, case)
        status = classify(real_result, oracle_result, case.expected,
                          semantics=manifest.semantics, oracle=manifest.oracle,
                          requires=case.requires, divergence_link=link)
        results.append(CaseResult(status=status, pattern=case.pattern,
                                  real_result=real_result, oracle_result=oracle_result))
    return CorpusReport(corpus=manifest.origin, manifest=manifest, results=results)


# --- loading manifests and cases --------------------------------------------------------------------

def load_manifest(obj):
    """Build a validated :class:`Manifest` from a dict (e.g. a parsed TOML ``[manifest]`` table)."""
    fields = {name: obj.get(name, "") for name in
              ("origin", "sha256", "license", "attribution", "retrieved", "semantics",
               "type", "api", "oracle", "notes")}
    return Manifest(**fields).validate()


def load_cases_toml(path):
    """Load (manifest, cases) from a native TOML corpus file (the rust-regex-shaped format).

    Expects a ``[manifest]`` table and an array of ``[[case]]`` tables (pattern / input / expected /
    flags / requires / status_expected). Returns ``(Manifest, list[Case])``.
    """
    with open(path, "rb") as handle:
        doc = tomllib.load(handle)
    manifest = load_manifest(doc.get("manifest", {}))
    cases = []
    for entry in doc.get("case", []):
        case = Case(pattern=entry["pattern"], input=entry.get("input"),
                    expected=entry.get("expected"), flags=list(entry.get("flags", [])),
                    requires=entry.get("requires", ""), status_expected=entry.get("status_expected"))
        cases.append(case.validate())
    return manifest, cases


def _rust_match(match):
    """Normalise a rust-regex match entry — ``[s, e]`` (group 0) or ``[[s0,e0], [s1,e1], …]``."""
    if match and isinstance(match[0], int):
        return {"span": list(match), "groups": []}
    span = list(match[0]) if match[0] is not None else [-1, -1]
    groups = [([-1, -1] if g is None else list(g)) for g in match[1:]]
    return {"span": span, "groups": groups}


def load_cases_rust_toml(path):
    """Adapter for a rust-regex ``testdata`` ``.toml`` file (``finditer`` semantics — the ``matches``
    field is every non-overlapping match).

    Returns ``(cases, filtered)``: `filtered` counts the ``[[test]]`` entries skipped as **out of the
    API we offer** (``anchored`` / ``bounds`` / ``match-limit`` / non-UTF-8), which is import-time
    filtering, distinct from any runtime status. ``unicode = false`` maps to the ``ascii`` flag and
    ``case-insensitive`` to ``icase``.
    """
    with open(path, "rb") as handle:
        doc = tomllib.load(handle)
    cases, filtered = [], 0
    for entry in doc.get("test", []):
        if (entry.get("anchored") or "bounds" in entry or "match-limit" in entry
                or entry.get("utf8") is False or not isinstance(entry.get("haystack"), str)):
            filtered += 1
            continue
        flags = []
        if entry.get("unicode") is False:
            flags.append("ascii")
        if entry.get("case-insensitive"):
            flags.append("icase")
        expected = [_rust_match(match) for match in entry.get("matches", [])]
        cases.append(Case(pattern=entry["regex"], input=entry["haystack"],
                          expected=expected, flags=flags))
    return cases, filtered


def load_cases_dat(path, manifest, *, encoding="latin-1"):
    """Adapter for a Fowler / Go ``testregex`` ``.dat`` corpus into :class:`Case` records.

    Each data line is tab-separated: ``flags  pattern  input  expected-spans …``. Comment lines
    (starting ``#``) and ``NOTE`` / blank lines are skipped. ``expected`` is a `(start,end)` span or
    ``None`` for the ``NOMATCH`` sentinel; the caller supplies the :class:`Manifest` (the ``.dat`` file
    carries no provenance of its own).
    """
    cases = []
    with open(path, "r", encoding=encoding) as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if not line or line.startswith("#") or line.startswith("NOTE"):
                continue
            fields = line.split("\t")
            if len(fields) < 4:
                continue
            _flags, pattern, text, expected = fields[0], fields[1], fields[2], fields[3]
            text = "" if text == "NULL" else text
            span = None if expected in ("NOMATCH", "NULL") else _parse_dat_span(expected)
            cases.append(Case(pattern=pattern, input=text,
                              expected=None if span is None else {"span": list(span), "groups": []}))
    return cases


def _parse_dat_span(field):
    """Parse a Fowler expected field's whole-match span ``(a,b)`` (the first group), else ``None``."""
    first = field.split(")")[0].lstrip("(")
    try:
        start, end = first.split(",")
        return int(start), int(end)
    except ValueError:
        return None
