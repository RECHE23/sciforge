#!/bin/sh
# Self-test for lint/misra.mk: the clang-tidy version it names is the one CI installs. Run via `make lint-config`,
# which passes MISRA_TIDY_VERSION as make read it. A version written anywhere else is a second source that drifts
# the day the pin moves, which is how a local analysis and CI's came to disagree.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root="$here/../.."
mk="$root/lint/misra.mk"
fail=0

# 1) The file names a version, as a number.
from_file=$(sed -n 's/^MISRA_TIDY_VERSION *:= *//p' "$mk")
case "$from_file" in
  ''|*[!0-9]*) echo "misra-pin: FAIL — lint/misra.mk names no numeric MISRA_TIDY_VERSION ('$from_file')" >&2; fail=1;;
esac

# 2) The workflows' reading of the file (the same sed) is what make read.
if [ "$from_file" != "${MISRA_TIDY_VERSION:-}" ]; then
  echo "misra-pin: FAIL — the workflows read '$from_file' from lint/misra.mk, make read '${MISRA_TIDY_VERSION:-}'" >&2
  fail=1
fi

# 3) No workflow installs a version of its own.
if grep -n 'clang-tidy-[0-9]' "$root"/.github/workflows/*.yml >&2; then
  echo "misra-pin: FAIL — a workflow names a clang-tidy version; read MISRA_TIDY_VERSION from lint/misra.mk" >&2
  fail=1
fi

[ "$fail" -eq 0 ] && echo "misra-pin: OK — clang-tidy $from_file, named once in lint/misra.mk"
exit "$fail"
