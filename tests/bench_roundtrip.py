"""Round-trip checker for the C++ emitter (tests/bench_emit.cpp).

Reads the emitter's stdout, ingests it with the *Python* sciforge.bench (run_from_json), and
asserts the known Run came through intact — same samples, the domain fields as first-class
extra, and the result. This proves the C++ emit_run output is exactly schema.run_to_json, so
load_run can read what any *_bench.cpp produces. Exits non-zero on any divergence.
"""

import json
import sys

from sciforge.bench import run_from_json


def _close(a, b):
    # The contract is %.6g precision; tolerate that rather than demanding bit-equality.
    return abs(a - b) <= 1e-6 * max(1.0, abs(a))


def _find_run(text):
    """The emitter prints the Run JSON on its own line (plus the test summary); pick the line
    that parses as a dict with 'cases'."""
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict) and "cases" in obj:
            return run_from_json(obj)
    return None


def main():
    run = _find_run(sys.stdin.read())
    if run is None:
        print("roundtrip: no Run JSON found on stdin", file=sys.stderr)
        return 1

    errors = []
    if run.meta.get("bench") != "selftest":
        errors.append(f"meta.bench = {run.meta.get('bench')!r}")
    if len(run.cases) != 2:
        errors.append(f"expected 2 cases, got {len(run.cases)}")
    else:
        alpha, beta = run.cases
        if alpha.name != "alpha" or alpha.unit != "s":
            errors.append(f"alpha name/unit = {alpha.name!r}/{alpha.unit!r}")
        if not _samples_match(alpha.samples, [1e-6, 2e-6, 3e-6]):
            errors.append(f"alpha.samples = {alpha.samples}")
        if alpha.extra.get("family") != "x":           # domain field survived as a sibling
            errors.append(f"alpha.extra.family = {alpha.extra.get('family')!r}")
        if alpha.result is not None:                   # no result emitted -> None
            errors.append(f"alpha.result = {alpha.result!r}")
        if not _samples_match(beta.samples, [4e-6, 5e-6]):
            errors.append(f"beta.samples = {beta.samples}")
        if beta.result != "ok":
            errors.append(f"beta.result = {beta.result!r}")
        if beta.extra.get("size") != 1024:
            errors.append(f"beta.extra.size = {beta.extra.get('size')!r}")

    if errors:
        print("roundtrip FAIL: " + "; ".join(errors), file=sys.stderr)
        return 1
    print("roundtrip OK: 2 cases, domain fields + result preserved, samples within %.6g precision")
    return 0


def _samples_match(got, expected):
    return len(got) == len(expected) and all(_close(a, b) for a, b in zip(got, expected))


if __name__ == "__main__":
    sys.exit(main())
