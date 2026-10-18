#!/usr/bin/env bash
# run-gate.sh — one verification tier of a SciForge project: its make steps in order, fail-fast, timed.
#
#   run-gate.sh <tier> <build-dir> <make> <step>...
#
# Each <step> is a make target of the calling project (a target, not a command line: a step that needs
# arguments is a target of its own, so that `make <step>` reproduces it alone). The steps run in order and
# the first red one stops the tier; a summary gives each step's time.
#
# Two gates in one tree share its build directory and contend for the machine, and a timing step then reads
# red for a reason the code has nothing to do with. So a tier takes <build-dir>/.gate.lock first and refuses
# while another live gate holds it; a lock whose holder is gone (killed, so its trap never ran) is reclaimed.
#
# A gate whose tree moved under it has judged nothing: early steps read one tree, late steps another. The
# tier records HEAD and `git status --porcelain` before its first step and compares them after its last; a
# difference voids the verdict (exit 1), whatever the steps said. HEAD belongs in the record because the
# likeliest change during a long gate is a commit, which leaves the porcelain as clean as it found it.
#
# Exit status: 0 when every step passed on a still tree, 1 otherwise; 2 for a usage error.
set -u

if [ "$#" -lt 4 ]; then
  echo "usage: run-gate.sh <tier> <build-dir> <make> <step>..." >&2
  exit 2
fi
tier=$1
build=$2
make_cmd=$3
shift 3

mkdir -p "$build" || exit 2
lock="$build/.gate.lock"
if ! mkdir "$lock" 2>/dev/null; then
  holder="$(cat "$lock/pid" 2>/dev/null || true)"
  if [ -n "$holder" ] && ! kill -0 "$holder" 2>/dev/null; then
    echo "check-$tier: reclaiming $lock -- its holder (pid $holder) is gone."
    rm -rf "$lock"
    mkdir "$lock" || exit 1
  else
    echo "check-$tier: REFUSING -- another gate holds $lock (pid ${holder:-unrecorded}). Wait for it:"
    echo "  two gates in one tree share its build and contend for the machine. A lock with no pid is not"
    echo "  reclaimed (an absent pid is no proof its holder is gone): remove it by hand once you know."
    exit 1
  fi
fi
echo $$ > "$lock/pid"
trap 'rm -rf "$lock" 2>/dev/null || true' EXIT INT TERM

snapshot() {
  git rev-parse HEAD 2>/dev/null
  git status --porcelain 2>/dev/null
}
before="$(snapshot)"

summary=()
total_start=$SECONDS
status=0
for step in "$@"; do
  echo "── check-$tier: $step"
  start=$SECONDS
  if $make_cmd --no-print-directory "$step"; then
    summary+=("$(printf '  %-28s %5ds' "$step" $((SECONDS - start)))")
  else
    summary+=("$(printf '  %-28s %5ds  FAILED' "$step" $((SECONDS - start)))")
    status=1
    break
  fi
done

echo ""
echo "check-$tier: $((SECONDS - total_start))s"
printf '%s\n' "${summary[@]}"
if [ "$(snapshot)" != "$before" ]; then
  echo ""
  echo "check-$tier: THE TREE CHANGED WHILE THIS GATE RAN -- the verdict above is void. Re-run on a still tree."
  exit 1
fi
if [ "$status" -ne 0 ]; then
  echo "check-$tier: FAILED"
  exit 1
fi
echo "check-$tier: PASS"
