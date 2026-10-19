#!/usr/bin/env bash
# check-pins.sh — fail on SciForge pin drift across a consumer repo.
#
# A consumer references SciForge four ways, and they must all name the SAME version:
#   1. a reusable-workflow pin      uses: RECHE23/sciforge/.github/workflows/X.yml@vVERSION
#   2. a reusable-workflow input    sciforge-ref: vVERSION
#   3. a direct checkout            repository: RECHE23/sciforge  +  ref: vVERSION   (same step, either order)
#   4. the CMake fetch tag          set(SCIFORGE_TAG "vVERSION" ...)                (CMakeLists.txt, cmake/*.cmake)
# One file bumped and another not has shipped mismatched substrates before, and a CMake tag can sit
# versions behind every workflow without any of them noticing. This lint collects every pinned version
# and fails if more than one distinct version appears. A value may be quoted.
#
# It also fails on a reference that pins no version — `@main`, a commit, `sciforge-ref: main`, a
# checkout of RECHE23/sciforge with no `ref:` — since it moves with SciForge's default branch, and on a
# pyproject `sciforge-build>=FLOOR` above the pinned version, which no build the pins describe meets.
#
# And it fails when the SciForge checkout it runs from (the sibling whose make/, lint/ and tools/ the consumer's
# Makefile reads) is not the pinned version, exactly and unmodified: a local gate would then run what no CI run of
# the pins does, and a consumer could come to rely on SciForge work no release has. SCIFORGE_UNPINNED_OK=1 turns
# that one into a warning, to work on SciForge and a consumer together; outside a git checkout it is skipped.
# SCIFORGE_CHECKOUT names another checkout to judge.
#
# Usage: check-pins.sh [REPO_ROOT]   (default: current directory)
#        check-pins.sh --self-test   (drives each form alone against a fixture repo)
set -euo pipefail

# Emit "version<TAB>file:line<TAB>form" for REF when it names a version, else an UNPINNED record.
pin_or_unpinned() {
  if printf '%s' "$1" | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    printf '%s\t%s\t%s\n' "$1" "$2" "$3"
  else
    printf 'UNPINNED\t%s\t%s %s\n' "$2" "$3" "${1:-(empty)}"
  fi
}

# Emit "version<TAB>file:line<TAB>form" for every SciForge pin found under $1, "UNPINNED<TAB>file:line<TAB>
# what" for a reference that pins no version, and "FLOOR<TAB>file:line<TAB>version" for a pyproject floor.
scan() {
  local root="$1" dir="$1/.github/workflows" f
  if [ -d "$dir" ]; then
    for f in "$dir"/*.yml "$dir"/*.yaml; do
      [ -e "$f" ] || continue
      # 1. reusable-workflow uses: ...@REF
      { grep -nE 'RECHE23/sciforge/[^@[:space:]]+@' "$f" || true; } | while IFS=: read -r ln rest; do
        v=$(printf '%s' "$rest" | sed -E 's/.*RECHE23\/sciforge\/[^@[:space:]]+@([^[:space:]"'"'"']*).*/\1/')
        pin_or_unpinned "$v" "$f:$ln" uses
      done
      # 2. sciforge-ref: REF
      { grep -nE 'sciforge-ref:' "$f" || true; } | while IFS=: read -r ln rest; do
        v=$(printf '%s' "$rest" | sed -E 's/.*sciforge-ref:[[:space:]]*["'"'"']?([^[:space:]"'"'"']*).*/\1/')
        pin_or_unpinned "$v" "$f:$ln" sciforge-ref
      done
      # 3. checkout of RECHE23/sciforge. A step starts at a list item ("- "); its `repository:` and
      #    `ref:` pair only inside that step, whichever comes first, so the ref of ANOTHER checkout step
      #    is never attributed to SciForge.
      awk -v file="$f" '
        function flush() {
          if (repo) {
            if (ref_v ~ /^v[0-9]+\.[0-9]+\.[0-9]+$/) { printf "%s\t%s:%s\tcheckout\n", ref_v, file, ref_ln }
            else if (ref_v == "") { printf "UNPINNED\t%s:%s\tcheckout with no ref\n", file, repo_ln }
            else { printf "UNPINNED\t%s:%s\tcheckout ref %s\n", file, ref_ln, ref_v }
          }
          repo = 0; repo_ln = 0; ref_v = ""; ref_ln = 0
        }
        /^[[:space:]]*-[[:space:]]/ { flush() }
        /repository:[[:space:]]*["\047]?RECHE23\/sciforge["\047]?([[:space:]]|$)/ { repo = 1; repo_ln = NR }
        /^[[:space:]]*ref:/ {
          v = $0; sub(/^[[:space:]]*ref:[[:space:]]*/, "", v); gsub(/["\047]/, "", v); sub(/[[:space:]].*$/, "", v)
          ref_v = v; ref_ln = NR
        }
        END { flush() }
      ' "$f"
    done
  fi
  # 5. pyproject: the sciforge-build floor a Python build requires.
  if [ -e "$root/pyproject.toml" ]; then
    { grep -nE 'sciforge-build[[:space:]]*>=' "$root/pyproject.toml" || true; } | while IFS=: read -r ln rest; do
      v=$(printf '%s' "$rest" | sed -E 's/.*sciforge-build[[:space:]]*>=[[:space:]]*([0-9][0-9.]*).*/\1/')
      printf 'FLOOR\t%s:%s\t%s\n' "$root/pyproject.toml" "$ln" "$v"
    done
  fi
  # 4. CMake: set(SCIFORGE_TAG "vVERSION" ...) in the top-level list file or a cmake/ module.
  for f in "$root/CMakeLists.txt" "$root"/cmake/*.cmake; do
    [ -e "$f" ] || continue
    { grep -nE 'set\([[:space:]]*SCIFORGE_TAG[[:space:]]+"?v[0-9]+\.[0-9]+\.[0-9]+' "$f" || true; } | while IFS=: read -r ln rest; do
      v=$(printf '%s' "$rest" | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | head -1)
      printf '%s\t%s:%s\tcmake\n' "$v" "$f" "$ln"
    done
  done
}

# Judge $1; print the verdict; return 0 (agreement or nothing pinned) or 1 (drift).
check() {
  local root="$1" records pins unpinned floors versions n floor loc high
  records="$(scan "$root")"
  pins="$(printf '%s\n' "$records" | grep -E '^v' || true)"
  unpinned="$(printf '%s\n' "$records" | grep -E '^UNPINNED' || true)"
  floors="$(printf '%s\n' "$records" | grep -E '^FLOOR' || true)"
  if [ -n "$unpinned" ]; then
    echo "check-pins: UNPINNED — a SciForge reference follows a branch or a commit, not a version:"
    printf '%s\n' "$unpinned" | while IFS=$'\t' read -r _ loc what; do printf '  %s (%s)\n' "$loc" "$what"; done
    return 1
  fi
  if [ -z "$pins" ]; then
    echo "check-pins: no SciForge pins found under $root"
    return 0
  fi
  versions="$(printf '%s\n' "$pins" | cut -f1 | sort -u)"
  n="$(printf '%s\n' "$versions" | grep -c .)"
  if [ "$n" -gt 1 ]; then
    echo "check-pins: DRIFT — $n distinct SciForge versions pinned:"
    printf '%s\n' "$pins" | sort | while IFS=$'\t' read -r v loc form; do
      printf '  %-14s %s (%s)\n' "$v" "$loc" "$form"
    done
    return 1
  fi
  if [ -n "$floors" ]; then
    while IFS=$'\t' read -r _ loc floor; do
      high="$(printf '%s\n%s\n' "$floor" "${versions#v}" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
      if [ "$floor" != "${versions#v}" ] && [ "$high" = "$floor" ]; then
        echo "check-pins: FLOOR — $loc requires sciforge-build>=$floor, above the pinned $versions"
        return 1
      fi
    done <<< "$floors"
  fi
  sibling "$root" "$versions" || return 1
  echo "check-pins: OK — SciForge pinned at $versions across $(printf '%s\n' "$pins" | grep -c .) site(s) under $root"
  return 0
}

# Judge the SciForge checkout this script runs from (or SCIFORGE_CHECKOUT) against the version $2 that $1 pins.
sibling() {
  local root="$1" version="$2" checkout at
  checkout="${SCIFORGE_CHECKOUT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
  if [ ! -d "$checkout" ] || [ "$(cd "$root" && pwd -P)" = "$(cd "$checkout" && pwd -P)" ] \
     || ! git -C "$checkout" rev-parse --git-dir > /dev/null 2>&1; then
    return 0 # SciForge judging its own pins, or a checkout without its history
  fi
  at="$(git -C "$checkout" describe --tags --exact-match HEAD 2> /dev/null || git -C "$checkout" describe --tags --always 2> /dev/null)"
  if [ -n "$(git -C "$checkout" status --porcelain --untracked-files=no 2> /dev/null)" ]; then
    at="$at, modified"
  fi
  if [ "$at" = "$version" ]; then
    return 0
  fi
  if [ "${SCIFORGE_UNPINNED_OK:-0}" = 1 ]; then
    echo "check-pins: warning — the SciForge checkout $checkout is at $at, the pins name $version (SCIFORGE_UNPINNED_OK=1)"
    return 0
  fi
  echo "check-pins: SIBLING — the SciForge checkout $checkout is at $at, the pins name $version: check it out at"
  echo "  $version (git -C $checkout checkout $version), or pin the release it is, or set SCIFORGE_UNPINNED_OK=1 to"
  echo "  work on both together"
  return 1
}

# Each drift case pins v1.0.0 through a `uses:` line and ONE other form at v2.0.0, so exactly that form
# can produce the drift, and the verdict must name it at v2.0.0. A form that stopped being read leaves
# its case green, which fails the self-test.
self_test() {
  local tmp failures=0 base
  tmp="$(mktemp -d)"
  # The checkout every case is judged against: a repository at v1.0.0, the version the cases agree on.
  git init -q "$tmp/sf"
  git -C "$tmp/sf" -c user.name=t -c user.email=t@t commit -q --allow-empty -m one
  git -C "$tmp/sf" tag v1.0.0
  printf 'x\n' > "$tmp/sf/tracked"
  git -C "$tmp/sf" add tracked
  git -C "$tmp/sf" -c user.name=t -c user.email=t@t commit -q -m tracked
  git -C "$tmp/sf" tag -f v1.0.0 > /dev/null
  export SCIFORGE_CHECKOUT="$tmp/sf"
  base='jobs:
  build:
    uses: RECHE23/sciforge/.github/workflows/build-cpp.yml@v1.0.0
'
  expect() { # expect <name> <ok|drift|unpinned|floor> <pattern the verdict must hold, or empty> [<path> <content>]...
    local name="$1" want="$2" pattern="$3" out rc
    shift 3
    rm -rf "$tmp/repo"
    mkdir -p "$tmp/repo/.github/workflows" "$tmp/repo/cmake"
    while [ "$#" -gt 0 ]; do printf '%s' "$2" > "$tmp/repo/$1"; shift 2; done
    set +e
    out="$(check "$tmp/repo")"
    rc=$?
    set -e
    if { [ "$want" = ok ] && [ "$rc" -ne 0 ]; } || { [ "$want" != ok ] && [ "$rc" -ne 1 ]; } \
       || { [ "$want" != ok ] && ! printf '%s' "$out" | grep -q "$(printf '%s' "$want" | tr '[:lower:]' '[:upper:]')"; } \
       || { [ -n "$pattern" ] && ! printf '%s' "$out" | grep -qE "$pattern"; }; then
      echo "SELF-TEST FAILED: $name (rc=$rc)"
      printf '%s\n' "$out" | sed 's/^/    /'
      failures=$((failures + 1))
    fi
  }
  expect "agreement across every form" ok "" \
    .github/workflows/ci.yml "$base    with:
      sciforge-ref: v1.0.0
  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          repository: RECHE23/sciforge
          ref: v1.0.0
" CMakeLists.txt 'set(SCIFORGE_TAG "v1.0.0" CACHE STRING "tag")
'
  expect "uses: drift" drift "v2\\.0\\.0 .*\\(uses\\)" \
    .github/workflows/ci.yml "$base" .github/workflows/other.yml 'jobs:
  x:
    uses: RECHE23/sciforge/.github/workflows/lint-cpp.yml@v2.0.0
'
  expect "sciforge-ref drift" drift "v2\\.0\\.0 .*\\(sciforge-ref\\)" \
    .github/workflows/ci.yml "$base    with:
      sciforge-ref: v2.0.0
"
  expect "checkout, ref after repository" drift "v2\\.0\\.0 .*\\(checkout\\)" \
    .github/workflows/ci.yml "$base  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          repository: RECHE23/sciforge
          ref: v2.0.0
"
  expect "checkout, ref before repository" drift "v2\\.0\\.0 .*\\(checkout\\)" \
    .github/workflows/ci.yml "$base  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          ref: v2.0.0
          repository: RECHE23/sciforge
"
  expect "another step's ref is not SciForge's" ok "" \
    .github/workflows/ci.yml "$base  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          repository: RECHE23/sciforge
          ref: v1.0.0
      - uses: actions/checkout@v6
        with:
          repository: someone/else
          ref: v2.0.0
"
  expect "quoted values are read" drift "v2\\.0\\.0 .*\\(sciforge-ref\\)" \
    .github/workflows/ci.yml "$base    with:
      sciforge-ref: \"v2.0.0\"
"
  expect "uses: a branch" unpinned "uses main" \
    .github/workflows/ci.yml "$base" .github/workflows/other.yml 'jobs:
  x:
    uses: RECHE23/sciforge/.github/workflows/lint-cpp.yml@main
'
  expect "sciforge-ref: a commit" unpinned "sciforge-ref 0123abc" \
    .github/workflows/ci.yml "$base    with:
      sciforge-ref: 0123abc
"
  expect "checkout with no ref" unpinned "checkout with no ref" \
    .github/workflows/ci.yml "$base  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          repository: RECHE23/sciforge
"
  expect "checkout of a branch" unpinned "checkout ref main" \
    .github/workflows/ci.yml "$base  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          repository: 'RECHE23/sciforge'
          ref: 'main'
"
  expect "a floor above the pin" floor "sciforge-build>=1\\.2" \
    .github/workflows/ci.yml "$base" pyproject.toml '[build-system]
requires = ["setuptools>=68", "sciforge-build>=1.2.0"]
'
  expect "a floor at or below the pin" ok "" \
    .github/workflows/ci.yml "$base" pyproject.toml '[build-system]
requires = ["setuptools>=68", "sciforge-build>=0.9.5"]
'
  expect "a floor equal to the pin" ok "" \
    .github/workflows/ci.yml "$base" pyproject.toml '[build-system]
requires = ["sciforge-build>=1.0.0"]
'
  expect "CMake tag drift (top-level list file)" drift "v2\\.0\\.0 .*\\(cmake\\)" \
    .github/workflows/ci.yml "$base" CMakeLists.txt 'set(SCIFORGE_TAG "v2.0.0" CACHE STRING "tag")
'
  expect "CMake tag drift (cmake/ module)" drift "v2\\.0\\.0 .*\\(cmake\\)" \
    .github/workflows/ci.yml "$base" cmake/deps.cmake 'set(SCIFORGE_TAG v2.0.0)
'
  # The checkout itself: ahead of the pinned tag, modified, allowed, or no repository at all.
  local agreeing=".github/workflows/ci.yml"
  git -C "$tmp/sf" -c user.name=t -c user.email=t@t commit -q --allow-empty -m ahead
  expect "a checkout ahead of the pin" sibling "is at v1\.0\.0-1-g[0-9a-f]+, the pins name v1\.0\.0" "$agreeing" "$base"
  SCIFORGE_UNPINNED_OK=1 expect "a checkout ahead, allowed" ok "warning" "$agreeing" "$base"
  git -C "$tmp/sf" reset -q --hard v1.0.0
  printf 'y\n' > "$tmp/sf/tracked"
  expect "a modified checkout at the pin" sibling "is at v1\.0\.0, modified" "$agreeing" "$base"
  git -C "$tmp/sf" checkout -q -- tracked
  expect "a checkout at the pin" ok "OK" "$agreeing" "$base"
  mkdir -p "$tmp/no-repo"
  SCIFORGE_CHECKOUT="$tmp/no-repo" expect "no checkout history" ok "OK" "$agreeing" "$base"
  rm -rf "$tmp"
  if [ "$failures" -ne 0 ]; then
    echo "check-pins: self-test FAILED ($failures case(s))"
    return 1
  fi
  echo "check-pins: self-test OK — uses, sciforge-ref, checkout (either order), CMake list file and module each drift on their own; a foreign checkout's ref is not attributed; a quoted value is read; a branch, a commit or a missing ref is unpinned; a floor above the pin fails; a checkout ahead of the pin or modified fails, unless allowed"
}

if [ "${1:-}" = "--self-test" ]; then
  self_test
else
  check "${1:-.}"
fi
