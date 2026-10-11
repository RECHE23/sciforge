#!/bin/sh
# Self-test for the shared MISRA base (lint/clang-tidy-misra): proves the config is
# well-formed and still behaves, so an edit can't silently break a consumer. Run via
# `make lint-config`. Behavioural (version-robust) rather than --verify-config-strict:
# the base disables checks that exist in only one clang-tidy version (cert-dcl21-cpp on
# older, pro-bounds-avoid-unchecked-container-access on newer), so verify-config always
# warns "unknown check" on one version — we tolerate those warnings and gate on a real
# config error and on the fixture's expected findings instead.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
config="$here/../clang-tidy-misra"
probe="$here/probe.cpp"
CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

# 1) Config must parse (tolerate cross-version "unknown check" warnings; fail on error).
if "$CLANG_TIDY" --verify-config --config-file="$config" 2>&1 | grep -q ': error:'; then
  echo "lint-config: FAIL — lint/clang-tidy-misra has a config error" >&2
  "$CLANG_TIDY" --verify-config --config-file="$config" >&2 || true
  exit 1
fi

# 2) Behaviour: the fixture's expected findings hold, and they fail the run. A consumer's `make misra` gates on
#    clang-tidy's exit status, which is non-zero only because the base makes every finding an error.
rc=0
out=$("$CLANG_TIDY" --config-file="$config" "$probe" -- -std=c++20 2>&1) || rc=$?
fail=0
[ "$rc" -ne 0 ] || { echo "lint-config: FAIL — a finding no longer fails clang-tidy (WarningsAsErrors lost?)" >&2; fail=1; }
expect_present() { echo "$out" | grep -q "$1" || { echo "lint-config: FAIL — expected '$1' (base went vacant?)" >&2; fail=1; }; }
expect_absent()  { echo "$out" | grep -q "$1" && { echo "lint-config: FAIL — unexpected '$1' (deviation lost?)" >&2; fail=1; } || true; }

expect_present 'modernize-use-nullptr'   # (1) an enabled base check still fires
expect_absent  'probe.cpp:19:.*signed-bitwise'  # (2) IgnorePositiveIntegerLiterals still applied
expect_present 'probe.cpp:26:.*signed-bitwise'  # (2b) ...on a check that is still on
expect_absent  'avoid-magic-numbers'     # (3) a documented base deviation still applied

# 3) A clean file passes: the exit status above is the findings', not a config that fails everything.
clean=$(mktemp "${TMPDIR:-/tmp}/lint-clean.XXXXXX.cpp")
trap 'rm -f "$clean"' EXIT
printf 'int main()\n{\n  return 0;\n}\n' > "$clean"
"$CLANG_TIDY" --config-file="$config" "$clean" -- -std=c++20 >/dev/null 2>&1 \
  || { echo "lint-config: FAIL — a clean file fails the base" >&2; fail=1; }

[ "$fail" -eq 0 ] && echo "lint-config: OK — shared MISRA base parses and behaves"
exit "$fail"
