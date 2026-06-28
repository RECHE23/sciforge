"""The benchmark exchange format: a Run of Cases, each carrying only raw samples.

A bencher emits **raw measurements** — nothing derived. A :class:`Case` holds the per-
operation `samples` (raw floats) plus a free-form bag of domain fields (the family a case
belongs to, the input size, an engine name, …); a :class:`Run` bundles the cases with a
`meta` dict (timestamp, platform, versions). Every statistic — median, IQR, CI, ratio,
geomean — is **derived by the reporter** from the raw samples with a fixed bootstrap seed,
so two reporters reading the same Run agree exactly. Storing only raw data keeps the format
methodology-neutral: the CI method can change without re-running the benchmark.

`write_run` / `load_run` are a plain JSON round-trip; the free domain fields survive it.
"""

import json
from dataclasses import dataclass, field

#: Case keys with a dedicated attribute; anything else in the JSON object is a domain field.
_CASE_KEYS = ("name", "unit", "samples", "result")


@dataclass
class Case:
    """One measured case: raw `samples` (in `unit`) plus optional `result` and free fields.

    `extra` holds the domain-specific fields (e.g. ``family``, ``size``, ``engine``) that the
    reporter groups and labels by; it is preserved verbatim across a JSON round-trip.
    """

    name: str
    unit: str
    samples: list
    result: object = None
    extra: dict = field(default_factory=dict)


@dataclass
class Run:
    """A benchmark run: a `meta` dict (free-form) and the list of measured `cases`."""

    meta: dict = field(default_factory=dict)
    cases: list = field(default_factory=list)


def _case_to_json(case):
    obj = {"name": case.name, "unit": case.unit, "samples": list(case.samples)}
    if case.result is not None:
        obj["result"] = case.result
    obj.update(case.extra)  # domain fields are first-class siblings, not nested
    return obj


def _case_from_json(obj):
    data = dict(obj)
    extra = {key: value for key, value in data.items() if key not in _CASE_KEYS}
    return Case(name=data["name"],
                unit=data["unit"],
                samples=list(data["samples"]),
                result=data.get("result"),
                extra=extra)


def run_to_json(run):
    """Return the plain-dict (JSON-ready) form of `run`."""
    return {"meta": dict(run.meta), "cases": [_case_to_json(case) for case in run.cases]}


def run_from_json(obj):
    """Rebuild a :class:`Run` from its plain-dict form."""
    return Run(meta=dict(obj.get("meta", {})),
               cases=[_case_from_json(case) for case in obj.get("cases", [])])


def write_run(run, path):
    """Write `run` to `path` as JSON (raw samples + meta + domain fields, nothing derived)."""
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(run_to_json(run), handle, indent=2)


def load_run(path):
    """Read a :class:`Run` back from the JSON file at `path`."""
    with open(path, "r", encoding="utf-8") as handle:
        return run_from_json(json.load(handle))
