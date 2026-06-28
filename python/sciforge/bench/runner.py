"""The collector: time a callable into a raw-sample :class:`Case`.

`collect` is deliberately the *only* thing here — it measures, it does not compare. It
disables the garbage collector for the duration, warms the callable once (so first-call
effects do not leak into the samples), auto-sizes an inner batch so each timed batch lasts
about `batch_target` seconds (amortising `perf_counter` overhead), and records `samples`
per-operation times. Everything downstream — medians, CIs, ratios, a verdict — is the
reporter's job, working from the raw samples this returns.
"""

import gc
from time import perf_counter

from .schema import Case


def fmt(seconds, width=0):
    """Format a duration (seconds) as ns / µs / ms, picking the readable unit.

    `width` right-justifies the number (with spaces) for column alignment in a table; leave
    it 0 for no padding. Pure presentation, domain-free — shared so consumers stop each
    carrying their own copy.
    """
    if seconds < 1e-6:
        return f"{seconds * 1e9:{width}.0f} ns"
    if seconds < 1e-3:
        return f"{seconds * 1e6:{width}.1f} µs"
    return f"{seconds * 1e3:{width}.2f} ms"


def collect(label, fn, samples=40, batch_target=0.005):
    """Time `fn` into a raw-sample :class:`Case` (per-operation seconds).

    Disables gc, warms `fn` once, sizes an inner batch to ~`batch_target` seconds, then
    records `samples` batch-averaged per-call times. Returns a :class:`Case` named `label`
    with `unit="s"` and the raw samples — no statistics are computed here.
    """
    was_enabled = gc.isenabled()
    gc.disable()
    try:
        fn()  # warm up: discard first-call effects (imports, lazy init, cold caches)
        start = perf_counter()
        fn()
        once = max(perf_counter() - start, 1e-9)
        batch = max(1, int(batch_target / once))
        raw = []
        for _ in range(samples):
            start = perf_counter()
            for _ in range(batch):
                fn()
            raw.append((perf_counter() - start) / batch)
    finally:
        if was_enabled:
            gc.enable()
    return Case(name=label, unit="s", samples=raw)
