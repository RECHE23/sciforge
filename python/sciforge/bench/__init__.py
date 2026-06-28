"""SciForge benchmark substrate: dep-free stats, an exchange schema, and a collector.

Development-only infrastructure shared across the ecosystem's benchmarks (real-regex,
scinum, …). Pure standard library; **never shipped** — consumers put ``../sciforge/python``
on ``PYTHONPATH`` (the sibling-checkout convention, mirroring the shared lint/ and
include/ dirs). The bencher emits raw samples (:func:`collect` → :class:`Case`); the
reporter derives every statistic from them.
"""

from .runner import collect, fmt
from .schema import Case, Run, load_run, run_from_json, run_to_json, write_run
from .stats import (
    ascii_boxplot,
    ascii_ecdf,
    bootstrap_ci,
    geomean_ci,
    median_iqr,
    ratio_ci,
)

__all__ = [
    "collect",
    "fmt",
    "Case",
    "Run",
    "write_run",
    "load_run",
    "run_to_json",
    "run_from_json",
    "median_iqr",
    "bootstrap_ci",
    "ratio_ci",
    "geomean_ci",
    "ascii_boxplot",
    "ascii_ecdf",
]
