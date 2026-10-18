#!/usr/bin/env bash
# Self-test of tools/run-gate.sh and make/quality.mk, in a scratch git repository: every verdict the tiers can
# give, each provoked on purpose — a pass, a red step stopping the rest, a tree edited or committed mid-gate, a
# live lock refused, a dead holder's lock reclaimed, a lock without a pid refused, the tiers' step order, and
# coverage-gate's 100 % rule both ways.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
git init -q . && git config user.email t@t && git config user.name t
cat > Makefile <<MK
SCIFORGE := $here
QUALITY_COMMIT   := ok-a
QUALITY_PUSH     := ok-b
QUALITY_RELEASE  := ok-c
QUALITY_COVERAGE := fake-coverage
include \$(SCIFORGE)/make/quality.mk
ok-a ok-b ok-c:
	@echo "ran \$@" >> order.log
red:
	@echo "ran red" >> order.log; exit 3
edit:
	@echo x >> tracked.txt
commit:
	@echo y >> tracked.txt; git add tracked.txt; git commit -q -m mid
fake-coverage:
	@printf 'TOTAL  10  0  100.00%%  5  0  100.00%%  20  0  %s  8  0  100.00%%\n' "\$(LINES)"; exit \$(SUITE)
MK
printf 'order.log\nbuild/\n' > .gitignore
echo base > tracked.txt
git add -A && git commit -q -m init

fails=0
expect() { # expect <description> <wanted exit> <command...>
  local what=$1 want=$2; shift 2
  "$@" > out.log 2>&1; local got=$?
  if [ "$got" -ne "$want" ]; then echo "FAIL: $what (exit $got, wanted $want)"; sed 's/^/    /' out.log; fails=$((fails + 1)); fi
}
gate() { bash "$here/tools/run-gate.sh" "$@"; }

expect "every step green passes" 0 gate t build make ok-a ok-b
grep -q "check-t: PASS" out.log || { echo "FAIL: no PASS line"; fails=$((fails + 1)); }
rm -f order.log
expect "a red step fails the tier" 1 gate t build make ok-a red ok-b
[ "$(cat order.log)" = "$(printf 'ran ok-a\nran red')" ] || { echo "FAIL: steps after the red one ran"; fails=$((fails + 1)); }
expect "an edit mid-gate voids it" 1 gate t build make ok-a edit
grep -q "TREE CHANGED" out.log || { echo "FAIL: edit not reported"; fails=$((fails + 1)); }
git checkout -q -- tracked.txt
expect "a commit mid-gate voids it" 1 gate t build make commit
grep -q "TREE CHANGED" out.log || { echo "FAIL: commit not reported"; fails=$((fails + 1)); }

mkdir -p build/.gate.lock
sleep 30 &
live=$!
echo $live > build/.gate.lock/pid
expect "a live holder's lock is refused" 1 gate t build make ok-a
grep -q "REFUSING" out.log || { echo "FAIL: live lock not refused"; fails=$((fails + 1)); }
kill $live 2>/dev/null; wait $live 2>/dev/null
expect "a dead holder's lock is reclaimed" 0 gate t build make ok-a
grep -q "reclaiming" out.log || { echo "FAIL: dead lock not reclaimed"; fails=$((fails + 1)); }
mkdir -p build/.gate.lock
expect "a lock without a pid is refused" 1 gate t build make ok-a
rm -rf build/.gate.lock
expect "a usage error" 2 gate t build make

rm -f order.log
expect "check-release runs every tier in order" 0 make --no-print-directory check-release
[ "$(cat order.log)" = "$(printf 'ran ok-a\nran ok-b\nran ok-c')" ] || { echo "FAIL: tier order"; cat order.log; fails=$((fails + 1)); }
rm -f order.log
expect "check-push runs commit then push steps" 0 make --no-print-directory check-push
[ "$(cat order.log)" = "$(printf 'ran ok-a\nran ok-b')" ] || { echo "FAIL: push order"; fails=$((fails + 1)); }
expect "coverage-gate passes at 100 %" 0 make --no-print-directory coverage-gate LINES=100.00% SUITE=0
expect "coverage-gate fails when the suite fails at 100 % (make: 2)" 2 make --no-print-directory coverage-gate LINES=100.00% SUITE=1
expect "coverage-gate fails below 100 % (make: 2)" 2 make --no-print-directory coverage-gate LINES=99.98% SUITE=0
expect "coverage-gate fails without a TOTAL line (make: 2)" 2 make --no-print-directory coverage-gate QUALITY_COVERAGE=ok-a SUITE=0

if [ "$fails" -ne 0 ]; then echo "run-gate selftest: $fails FAILED"; exit 1; fi
echo "run-gate selftest: OK"
