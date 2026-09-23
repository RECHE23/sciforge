#!/usr/bin/env python3
"""Drive the framework meta-test child once per scenario, asserting the harness's FAILURE paths.

The child (tests/framework_selftest.cpp) runs one scenario named on argv and returns run_all()'s exit
code. This driver proves the classic blind spot is covered: that a failed assertion is actually counted,
that EXPECT_THROWS fails when nothing throws, that the exit code is non-zero on failure and zero on
success, and that the printed check counts are exact.

Usage: framework_selftest_driver.py <child-binary>
"""

import subprocess
import sys

# scenario -> (expected exit code, a substring the summary line must contain)
SCENARIOS = {
    "pass":               (0, "0 checks failed"),
    "expect_false":       (1, "1 checks failed"),
    "eq_mismatch":        (1, "1 checks failed"),
    "throws_on_nonthrow": (1, "1 checks failed"),
    "mixed_sign_equal":   (0, "2 checks passed | 0 checks failed"),
    "mixed_sign_negative": (1, "1 checks failed"),
    "counts":             (1, "2 checks passed | 1 checks failed"),
}


def main():
    if len(sys.argv) != 2:
        print("usage: framework_selftest_driver.py <child-binary>", file=sys.stderr)
        return 2
    child = sys.argv[1]
    failures = 0
    for scenario, (want_rc, want_str) in SCENARIOS.items():
        proc = subprocess.run([child, scenario], capture_output=True, text=True)
        if proc.returncode != want_rc:
            print(f"FAIL {scenario}: exit {proc.returncode} != {want_rc}")
            failures += 1
        if want_str not in proc.stdout:
            print(f"FAIL {scenario}: summary missing {want_str!r} in: {proc.stdout.strip()}")
            failures += 1
    if failures:
        print(f"framework-selftest: {failures} check(s) failed")
        return 1
    print(f"framework-selftest: all {len(SCENARIOS)} failure/success paths detected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
