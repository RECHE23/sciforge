"""Comparative benchmarking: time a *subject* against a *reference*, judged by a callable.

This is the layer where two implementations are weighed against each other — but it stays
domain-free. The only thing it knows is the **metrology**: gate correctness once, time the
two back-to-back so the ratios are paired, and aggregate the per-case ratios into a
CI-aware verdict. *What* is compared and *what counts as equal* are the consumer's: it
passes a `subject` callable, a `reference` callable, and an `equal(subject_result,
reference_result) -> bool` predicate (a closure capturing whatever context it needs — a
tolerance, a span normalization, a count). There is deliberately no Comparator hierarchy;
the callable is the whole extension point, so no domain (regex spans, float tolerance,
token counts) ever leaks into the core.

Convention: a ratio is ``reference_time / subject_time``, so ratio > 1 means the subject is
faster than the reference. The verdict calls the subject "faster" only when the bootstrap CI
of the geometric mean clears 1.0, "slower" only when it is wholly below, else "indecisive".
A result mismatch (the gate failed) is never a win — it is excluded from the timing and
fails the verdict.
"""

import statistics
from dataclasses import dataclass, field

from .runner import batch_time, calibrate
from .schema import Case
from .stats import geomean_ci

# gc is toggled around the paired timed region, exactly as the single-fn collector does.
import gc


def collect_pair(subject, reference, samples=40, batch_target=0.005):
    """Paired per-op time distributions for `subject` and `reference`.

    Each sample times the subject then the reference back-to-back (same machine state), so the
    per-sample ratio is paired. gc is disabled across the timed region. Returns
    (subject_times, reference_times, ratios) with ratio = reference_time / subject_time.
    """
    subject()
    reference()  # warm both before either is calibrated, so the pair starts on equal footing
    n_subject = calibrate(subject, batch_target)
    n_reference = calibrate(reference, batch_target)
    subject_times, reference_times, ratios = [], [], []
    was_enabled = gc.isenabled()
    gc.disable()
    try:
        for _ in range(samples):
            s = batch_time(subject, n_subject)
            r = batch_time(reference, n_reference)
            subject_times.append(s)
            reference_times.append(r)
            ratios.append(r / s)
    finally:
        if was_enabled:
            gc.enable()
    return subject_times, reference_times, ratios


def compare(label, subject, reference, equal, samples=40, batch_target=0.005, **fields):
    """Gate `subject` against `reference` with `equal`, then time them paired → a :class:`Case`.

    Calls `equal(subject(), reference())` once: a mismatch marks the case and skips timing (a
    fast wrong answer is not a benchmark win). Otherwise collects the paired distribution. Any
    extra keyword `fields` (e.g. ``family=``, ``key=``) ride along on the Case as domain fields.
    The Case carries the subject's raw samples plus, in `extra`, the reference samples, the
    paired ratios, and the `mismatch` flag.
    """
    mismatch = not equal(subject(), reference())
    if mismatch:
        return Case(name=label, unit="s", samples=[], result=None,
                    extra={"reference_samples": [], "ratios": [], "mismatch": True, **fields})
    subject_times, reference_times, ratios = collect_pair(subject, reference, samples, batch_target)
    return Case(name=label, unit="s", samples=subject_times, result=None,
                extra={"reference_samples": reference_times, "ratios": ratios,
                       "mismatch": False, **fields})


@dataclass
class Verdict:
    """The outcome of aggregating comparison cases: a CI-aware speed call plus a correctness gate.

    `classification` is one of "faster" / "slower" / "indecisive" / "no-data" (a neutral fact
    about where the CI sits). `passed` is the universal correctness gate — True iff no key case
    mismatched; whether "slower" should also fail is the *consumer's* policy, read off
    `classification`, not baked in here. `text` is a ready-to-print human summary.
    """

    geomean: float
    ci_low: float
    ci_high: float
    classification: str
    mismatched: list = field(default_factory=list)
    passed: bool = True
    text: str = ""


def verdict(cases, key=None, subject_label="subject", bootstrap=1000, rng=None):
    """Aggregate comparison `cases` into a CI-aware :class:`Verdict`.

    `key(case) -> bool` selects the cases that gate the verdict (default: those without a
    falsey ``key`` field). The geometric mean of the per-case median ratios gets a bootstrap CI;
    the subject is "faster" only if the CI clears 1.0, "slower" only if wholly below, else
    "indecisive". Any key-case mismatch is listed and fails `passed` (a wrong answer is never a
    win). `subject_label` names the subject in the text (data from the consumer, e.g. "REAL").
    """
    if key is None:
        def key(case):
            return bool(case.extra.get("key", True))
    key_cases = [case for case in cases if key(case)]
    mismatched = [case.name for case in key_cases if case.extra.get("mismatch")]
    medians = [statistics.median(case.extra["ratios"])
               for case in key_cases
               if not case.extra.get("mismatch") and case.extra.get("ratios")]
    passed = not mismatched

    if not medians:
        note = f"; {len(mismatched)} result mismatch(es): {', '.join(mismatched)}" if mismatched else ""
        return Verdict(0.0, 0.0, 0.0, "no-data", mismatched, passed,
                       f"no comparable key cases{note}")

    geomean, ci_low, ci_high = geomean_ci(medians, B=bootstrap, rng=rng)
    if ci_low > 1.0:
        classification = "faster"
        text = f"{subject_label} faster overall: {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}] (CI clears 1.0)"
    elif ci_high < 1.0:
        classification = "slower"
        text = f"{subject_label} slower overall: {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}] (CI below 1.0)"
    else:
        classification = "indecisive"
        text = f"indecisive: {geomean:.2f}x [{ci_low:.2f}-{ci_high:.2f}] (CI straddles 1.0)"
    if mismatched:
        text += f"  |  {len(mismatched)} result mismatch(es): {', '.join(mismatched)}"
    return Verdict(geomean, ci_low, ci_high, classification, mismatched, passed, text)
