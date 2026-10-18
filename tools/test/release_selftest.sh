#!/usr/bin/env bash
# Self-test of tools/release.sh through make/release.mk, in a scratch repository with a bare remote: a dry run leaves
# the tree as it found it; a release commits, tags (annotated with its CHANGELOG section) and pushes, the next one of
# the month numbered after it; each refusal provoked on purpose — a dirty tree, another branch, an empty Unreleased,
# a failing bump, a bump touching another file, a failing precheck — leaves the tree clean.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git init -q --bare "$work/remote.git"
git init -q -b main "$work/repo" 2>/dev/null || { git init -q "$work/repo" && git -C "$work/repo" checkout -q -b main; }
cd "$work/repo" || exit 1
git config user.email t@t && git config user.name t
git remote add origin "$work/remote.git"
cat > Makefile <<MK
SCIFORGE      := $here
RELEASE_FILES := version.txt
RELEASE_CI    :=
RELEASE_PRECHECK := \$(if \$(RED_PRECHECK),red-check,green-check)
include \$(SCIFORGE)/make/release.mk
release-bump:
	@echo "\$(VERSION)" > version.txt; \$(if \$(STRAY),echo x > other.txt;,) \$(if \$(FAIL_BUMP),exit 4,true)
green-check:
	@true
red-check:
	@exit 5
MK
echo 0 > version.txt
printf 'out.log\n' > .gitignore
printf '# Changelog\n\n## Unreleased\n\n### Added\n- the first thing.\n\n## 2000.1.0 — 2000-01-01\n\n- old.\n' > CHANGELOG.md
git add -A && git commit -q -m init && git push -q origin main

fails=0
expect() { # expect <description> <wanted exit> <command...>
  local what=$1 want=$2; shift 2
  "$@" > out.log 2>&1; local got=$?
  if [ "$got" -ne "$want" ]; then echo "FAIL: $what (exit $got, wanted $want)"; sed 's/^/    /' out.log; fails=$((fails + 1)); fi
}
clean() { [ -z "$(git status --porcelain)" ] || { echo "FAIL: $1 left the tree dirty:"; git status --porcelain; fails=$((fails + 1)); git checkout -q -- .; rm -f other.txt; }; }
year=$(date -u +%Y); month=$(date -u +%m | sed 's/^0//')

expect "a dry run passes" 0 make --no-print-directory release DRY_RUN=1
grep -q "Releasing v$year.$month.0" out.log || { echo "FAIL: dry run's version"; fails=$((fails + 1)); }
grep -q "| - the first thing." out.log || { echo "FAIL: dry run's tag body"; fails=$((fails + 1)); }
clean "a dry run"
expect "a failing bump is refused" 2 make --no-print-directory release DRY_RUN=1 FAIL_BUMP=1
clean "a failing bump"
expect "a bump touching another file is refused" 2 make --no-print-directory release DRY_RUN=1 STRAY=1
grep -q "outside RELEASE_FILES: other.txt" out.log || { echo "FAIL: stray file not named"; fails=$((fails + 1)); }
rm -f other.txt; clean "a stray bump"
expect "a red precheck is refused" 2 make --no-print-directory release RED_PRECHECK=1
clean "a red precheck"
echo dirt > version.txt
expect "a dirty tree is refused" 2 make --no-print-directory release DRY_RUN=1
git checkout -q -- version.txt
git checkout -q -b side
expect "another branch is refused" 2 make --no-print-directory release DRY_RUN=1
git checkout -q main

expect "a release" 0 make --no-print-directory release
[ "$(git log -1 --format=%s)" = "release: v$year.$month.0" ] || { echo "FAIL: release commit"; fails=$((fails + 1)); }
[ "$(cat version.txt)" = "$year.$month.0" ] || { echo "FAIL: bump"; fails=$((fails + 1)); }
git cat-file -t "v$year.$month.0" | grep -q tag || { echo "FAIL: tag not annotated"; fails=$((fails + 1)); }
git tag -l --format='%(contents)' "v$year.$month.0" | grep -q "the first thing" || { echo "FAIL: tag body"; fails=$((fails + 1)); }
git --git-dir="$work/remote.git" rev-parse -q --verify "refs/tags/v$year.$month.0" >/dev/null || { echo "FAIL: tag not pushed"; fails=$((fails + 1)); }
[ "$(git --git-dir="$work/remote.git" rev-parse main)" = "$(git rev-parse HEAD)" ] || { echo "FAIL: commit not pushed"; fails=$((fails + 1)); }
grep -qx "## $year.$month.0 — $(date -u +%Y-%m-%d)" CHANGELOG.md || { echo "FAIL: CHANGELOG heading"; fails=$((fails + 1)); }
clean "a release"

expect "an empty Unreleased is refused" 2 make --no-print-directory release DRY_RUN=1
clean "an empty Unreleased"
awk '/^## Unreleased$/{print; print ""; print "- the second thing."; next} {print}' CHANGELOG.md > c.tmp && mv c.tmp CHANGELOG.md
git commit -q -am "docs: second" && git push -q origin main
expect "the month's next release" 0 make --no-print-directory release SKIP_PRECHECK=1 RED_PRECHECK=1
git rev-parse -q --verify "refs/tags/v$year.$month.1" >/dev/null || { echo "FAIL: second release not .1"; fails=$((fails + 1)); }

if [ "$fails" -ne 0 ]; then echo "release selftest: $fails FAILED"; exit 1; fi
echo "release selftest: OK"
