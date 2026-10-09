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


#: Corpus flag name -> the attribute every re-like module exposes. The vocabulary itself is bounded
#: by ``Case.validate`` (:data:`sciforge.corpus.schema.FLAG_NAMES`); this maps it. NO DEFAULT: an
#: unmapped name raises (KeyError) and a missing attribute raises (AttributeError) -- never a silent
#: 0. A flag that compiles to nothing makes engine and oracle wrong identically, which a
#: real-vs-oracle comparison cannot see; that shipped once, on `icase`.
_FLAG_ATTR = {"ascii": "ASCII", "dotall": "DOTALL", "icase": "IGNORECASE",
              "multiline": "MULTILINE", "verbose": "VERBOSE"}


def re_like_engine(module):
    """Return a driver over a re-compatible module (``real`` or ``re``): (pattern, input, api, flags).

    Flags are mapped through :data:`_FLAG_ATTR`; an unknown name or a missing module attribute
    raises, loudly, instead of compiling as flag value 0.
    """
    def run(pattern, text, api, flags):
        flag_value = 0
        for name in flags:
            flag_value |= getattr(module, _FLAG_ATTR[name])
        try:
            compiled = module.compile(pattern, flag_value)
        except (module.error, OverflowError, RecursionError, MemoryError):
            return "error"  # the pattern is rejected (a syntax error, or a resource limit like a huge {n})
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
        # Out of the API we offer, or a rust-specific knob: an anchored / sub-region / match-limited /
        # non-UTF-8 search, an overlapping or non-leftmost-first match-kind, a rust escape-decode, a
        # compile-only test, or a custom line terminator. Filtered at import and counted.
        if (entry.get("anchored") or "bounds" in entry or "match-limit" in entry
                or entry.get("utf8") is False or entry.get("unescape")
                or "search-kind" in entry or "line-terminator" in entry
                or entry.get("compiles") is False
                or entry.get("match-kind") not in (None, "leftmost-first")
                or not isinstance(entry.get("haystack"), str)):
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


#: The control prefixes of a Fowler flag field (``?test``, ``{test`` …): Go strips one and runs the rest.
_DAT_CONTROL = "?&|;{}"
#: What Go's ``regexp.QuoteMeta`` escapes, for the literal (``L``) mode.
_DAT_META = frozenset("\\.+*?()|[]{}^$")
_DAT_ESCAPES = {"a": "\a", "b": "\b", "f": "\f", "n": "\n", "r": "\r", "t": "\t", "v": "\v",
                "\\": "\\", '"': '"'}


def _dat_unquote(field):
    """Expand a field's C escapes as Go's ``strconv.Unquote`` does for the ``$`` flag; ``None`` when Go
    would refuse it."""
    out, i = [], 0
    while i < len(field):
        char = field[i]
        if char != "\\":
            out.append(char)
            i += 1
            continue
        code = field[i + 1:i + 2]
        if code in _DAT_ESCAPES:
            out.append(_DAT_ESCAPES[code])
            i += 2
            continue
        width = {"x": 2, "u": 4, "U": 8}.get(code)
        digits, base, skip = (field[i + 2:i + 2 + width], 16, 2 + width) if width else (field[i + 1:i + 4], 8, 4)
        try:
            out.append(chr(int(digits, base)))
        except ValueError:
            return None
        if len(digits) != skip - (2 if width else 1):
            return None
        i += skip
    return "".join(out)


def _parse_dat_result(field):
    """Field 4 of a Fowler line, as Go's ``parseFowlerResult`` reads it: ``NOMATCH`` -> ``None``, a POSIX
    error code (an upper-case word) -> ``"error"``, else the match array ``(a,b)(c,d)…`` -> the whole-match
    span plus the group spans it lists, ``?`` read as -1. Returns ``(expected, ok)``."""
    if field == "NOMATCH":
        return None, True
    if "A" <= field[0] <= "Z":
        return "error", True
    values, rest = [], field
    while rest:
        if len(values) % 2 == 0:
            if not rest.startswith("("):
                return None, False
            rest, end = rest[1:], ","
        else:
            end = ")"
        stop = rest.find(end)
        if stop <= 0:
            return None, False
        token, rest = rest[:stop], rest[stop + 1:]
        try:
            values.append(-1 if token == "?" else int(token))
        except ValueError:
            return None, False
    if len(values) % 2:
        return None, False
    pairs = [values[k:k + 2] for k in range(0, len(values), 2)]
    return {"span": pairs[0], "groups": pairs[1:]}, True


def load_cases_dat(path, manifest, *, encoding="latin-1", with_filtered=False):
    """Adapter for a Fowler / Go ``testregex`` ``.dat`` corpus into :class:`Case` records, read as Go's
    ``regexp`` ``testFowler`` reads it.

    A line is fields separated by RUNS of tabs: flags, pattern, input, outcome, an optional comment.
    ``NULL`` is the empty string; a line with ``NIL`` is skipped. A flag field starting with a control
    character (``?&|;{}``) runs with that character stripped; ``:comment:flags`` runs its flags; a
    ``C`` / ``N[OTE]`` / ``T[EST]`` / numeric line is not a test. ``$`` expands C escapes in the pattern
    and input, ``SAME`` repeats the previous pattern, ``i`` sets ``icase``. Each mode letter the line
    names yields a case: ``E`` (extended, the pattern as written) and ``L`` (literal, the pattern
    quoted); the other modes (``B`` basic, ``A``, ``S``, ``K``) are not run, as in Go. The outcome is
    :func:`_parse_dat_result`'s. The caller supplies the :class:`Manifest` (the ``.dat`` file carries no
    provenance of its own).

    Returns the cases, or ``(cases, filtered)`` with ``with_filtered``: `filtered` counts the test
    lines that yield no case (only unsupported modes, ``NIL``, an escape or outcome Go cannot read).
    """
    cases, filtered, last_pattern = [], 0, ""
    with open(path, "r", encoding=encoding) as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = [part for part in line.split("\t") if part]
            if not fields:
                continue
            flags = fields[0]
            if flags[0] in _DAT_CONTROL:
                flags = flags[1:]
                if not flags:
                    continue
            elif flags[0] == ":":
                _comment, colon, flags = flags[1:].partition(":")
                if not colon:
                    continue
            elif flags[0] in "CNT0123456789":
                continue
            if len(fields) < 4 or "NIL" in fields:
                filtered += 1
                continue
            fields = ["" if part == "NULL" else part for part in fields]
            pattern, text = fields[1], fields[2]
            if "$" in flags:
                pattern, text = _dat_unquote(pattern), _dat_unquote(text)
                if pattern is None or text is None:
                    filtered += 1
                    continue
            if pattern == "SAME":
                pattern = last_pattern
            last_pattern = pattern
            expected, ok = _parse_dat_result(fields[3])
            modes = [mode for mode in flags if mode in "EL"]
            if not ok or not modes:
                filtered += 1
                continue
            case_flags = ["icase"] if "i" in flags else []
            for mode in modes:
                source = pattern if mode == "E" else "".join(
                    "\\" + char if char in _DAT_META else char for char in pattern)
                cases.append(Case(pattern=source, input=text, expected=expected,
                                  flags=list(case_flags)).validate())
    return (cases, filtered) if with_filtered else cases
