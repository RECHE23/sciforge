#!/usr/bin/env bash
# release.sh — cut a calendar-versioned release of a SciForge project: vYEAR.MONTH.PATCH, PATCH the count of the
# month's tags so far (the first of a month is .0).
#
# Driven by make/release.mk, which passes the project's settings in the environment:
#   RELEASE_MAKE       the make to run the project's targets with
#   RELEASE_FILES      the files the version bump may change (besides CHANGELOG.md)
#   RELEASE_BUMP       the project's make target that writes VERSION=<x> into them
#   RELEASE_PRECHECK   make targets that must pass first (benchmarks against the last release, …); skipped by
#                      SKIP_PRECHECK=1 and by a dry run
#   RELEASE_ADVISORY   make targets run in every mode, dry run included, whose failure warns and never blocks
#                      (benchmarks stamped against another version of a dependency, …)
#   RELEASE_CI         workflows whose run on HEAD must have succeeded (the tag publishes): none checked if empty
#   RELEASE_REMOTE     the remote to push to (origin)
#   DRY_RUN            1: every guard and the bump, the tag body shown, then the tree restored — nothing committed,
#                      tagged or pushed
#
# CHANGELOG.md keeps a `## Unreleased` section at its top; a release refuses an empty one, files its entries under
# `## <version> — <date>` and leaves `## Unreleased` above, empty. The tag is annotated with that section.
#
# The tree is restored on any exit before the commit (a failed bump, a refused guard, an interrupted dry run): it
# was clean when the release began, so every tracked change is the release's own.
set -u

die() { echo "release: $*" >&2; exit 1; }

make_cmd=${RELEASE_MAKE:-make}
files=${RELEASE_FILES:-}
bump=${RELEASE_BUMP:-}
precheck=${RELEASE_PRECHECK:-}
advisory=${RELEASE_ADVISORY:-}
workflows=${RELEASE_CI:-}
remote=${RELEASE_REMOTE:-origin}
dry=${DRY_RUN:-}

[ -n "$bump" ] || die "RELEASE_BUMP names no target (the one that writes the version into RELEASE_FILES)"
[ -f CHANGELOG.md ] || die "no CHANGELOG.md here"
branch=$(git symbolic-ref --short HEAD 2>/dev/null || true)
[ "$branch" = main ] || die "release from main only (on '${branch:-a detached HEAD}')"
[ -z "$(git status --porcelain)" ] || die "the working tree is not clean"

head=$(git rev-parse HEAD)
for workflow in $workflows; do
  state=$(gh run list --workflow "$workflow" --commit "$head" --json status,conclusion \
            --jq '.[0] | if . == null then "not run" elif .status != "completed" then .status else .conclusion end' 2>/dev/null) \
    || die "could not read workflow '$workflow' on $head (gh)"
  [ "$state" = success ] || die "workflow '$workflow' is '$state' on $head: tag only a commit whose CI is green"
done
[ -n "$dry" ] || git fetch --tags --quiet "$remote" || die "could not fetch the tags from '$remote'"

if [ -z "$dry" ] && [ -z "${SKIP_PRECHECK:-}" ]; then
  for target in $precheck; do
    $make_cmd --no-print-directory "$target" \
      || die "precheck '$target' failed (SKIP_PRECHECK=1 to release anyway, knowingly)"
  done
fi

for target in $advisory; do
  $make_cmd --no-print-directory "$target" || echo "release: WARN — advisory check '$target' failed; proceeding (it never blocks)"
done

year=$(date -u +%Y)
month=$(date -u +%m | sed 's/^0//')
today=$(date -u +%Y-%m-%d)
patch=$(git tag -l "v$year.$month.*" | wc -l | tr -d ' ')
version="$year.$month.$patch"
git rev-parse -q --verify "refs/tags/v$version" >/dev/null && die "tag v$version already exists"

awk '/^## Unreleased$/{f=1;next} f&&/^## /{exit 1} f&&NF{found=1;exit 0} END{if(!found)exit 1}' CHANGELOG.md \
  || die "'## Unreleased' in CHANGELOG.md is empty or missing: describe the release first"

committed=
restore() {
  [ -n "$committed" ] && return
  git checkout -- . 2>/dev/null || true
  rm -f CHANGELOG.md.tmp
}
trap restore EXIT INT TERM

echo "Releasing v$version ($today)${dry:+  [DRY_RUN — no commit, tag or push]}"
awk -v v="$version" -v d="$today" '/^## Unreleased$/{print; print ""; print "## " v " — " d; next} {print}' CHANGELOG.md \
  > CHANGELOG.md.tmp && mv CHANGELOG.md.tmp CHANGELOG.md
$make_cmd --no-print-directory "$bump" VERSION="$version" || die "the bump '$bump' failed"

allowed=" CHANGELOG.md $files "
stray=
while read -r _ path; do
  case "$allowed" in *" $path "*) ;; *) stray="$stray $path" ;; esac
done < <(git status --porcelain --untracked-files=all)
[ -z "$stray" ] || die "the bump changed files outside RELEASE_FILES:$stray"

body=$(awk -v h="## $version " 'index($0,h)==1{f=1;print;next} f&&/^## /{exit} f{print}' CHANGELOG.md)
if [ -n "$dry" ]; then
  echo "would commit 'release: v$version' (CHANGELOG.md $files) and tag v$version, its body:"
  printf '%s\n' "$body" | sed 's/^/    | /'
  echo "would push HEAD and v$version to $remote"
  exit 0
fi

# shellcheck disable=SC2086 # the files are a list
git add CHANGELOG.md $files || die "git add failed"
git commit -q -m "release: v$version" || die "the release commit failed"
committed=1
printf '%s\n' "$body" | git tag -a --cleanup=verbatim "v$version" -F - || die "tagging v$version failed (the release commit is local, not pushed)"
git push "$remote" HEAD "v$version" || die "the push failed: commit and tag v$version are local; push them by hand"
echo "release: v$version pushed"
