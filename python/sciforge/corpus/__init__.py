"""SciForge conformance-corpus substrate: the case contract and its exchange schema.

Development-only infrastructure shared across the ecosystem's conformance testing (real-regex, scilex,
…). Pure standard library; **never shipped** — consumers put ``../sciforge/python`` on ``PYTHONPATH``
(the sibling-checkout convention). The normative prose contract is in ``docs/corpus.md``; this package
is its machine half — the manifest and case records, and the :func:`classify` decision rules that turn
a run into exactly one of the five statuses.
"""

from .schema import (
    BUG,
    EXCLUDED_BY_DESIGN,
    INTENTIONAL_DIVERGENCE,
    OUT_OF_CONTRACT,
    PASS,
    STATUSES,
    Case,
    CaseResult,
    CorpusReport,
    Manifest,
    SchemaError,
    classify,
    is_empty_iteration_capture,
    load_report,
    report_from_json,
    report_to_json,
    write_report,
)

__all__ = [
    "PASS",
    "BUG",
    "INTENTIONAL_DIVERGENCE",
    "OUT_OF_CONTRACT",
    "EXCLUDED_BY_DESIGN",
    "STATUSES",
    "Manifest",
    "Case",
    "CaseResult",
    "CorpusReport",
    "SchemaError",
    "classify",
    "is_empty_iteration_capture",
    "report_to_json",
    "report_from_json",
    "write_report",
    "load_report",
]
