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
# and fails if more than one distinct version appears.
#
# Usage: check-pins.sh [REPO_ROOT]   (default: current directory)
#        check-pins.sh --self-test   (drives each form alone against a fixture repo)
set -euo pipefail

# Emit "version<TAB>file:line<TAB>form" for every SciForge pin found under $1.
scan() {
  local root="$1" dir="$1/.github/workflows" f
  if [ -d "$dir" ]; then
    for f in "$dir"/*.yml "$dir"/*.yaml; do
      [ -e "$f" ] || continue
      # 1. reusable-workflow uses: ...@vVERSION
      { grep -nE 'RECHE23/sciforge/[^@]+@v[0-9]+\.[0-9]+\.[0-9]+' "$f" || true; } | while IFS=: read -r ln rest; do
        v=$(printf '%s' "$rest" | grep -oE '@v[0-9]+\.[0-9]+\.[0-9]+' | tr -d '@')
        printf '%s\t%s:%s\tuses\n' "$v" "$f" "$ln"
      done
      # 2. sciforge-ref: vVERSION
      { grep -nE 'sciforge-ref:[[:space:]]*v[0-9]+\.[0-9]+\.[0-9]+' "$f" || true; } | while IFS=: read -r ln rest; do
        v=$(printf '%s' "$rest" | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+')
        printf '%s\t%s:%s\tsciforge-ref\n' "$v" "$f" "$ln"
      done
      # 3. checkout of RECHE23/sciforge. A step starts at a list item ("- "); its `repository:` and
      #    `ref:` pair only inside that step, whichever comes first, so the ref of ANOTHER checkout step
      #    is never attributed to SciForge.
      awk -v file="$f" '
        function flush() {
          if (repo && ref_v != "") { printf "%s\t%s:%s\tcheckout\n", ref_v, file, ref_ln }
          repo = 0; ref_v = ""; ref_ln = 0
        }
        /^[[:space:]]*-[[:space:]]/ { flush() }
        /repository:[[:space:]]*RECHE23\/sciforge([[:space:]]|$)/ { repo = 1 }
        match($0, /ref:[[:space:]]*v[0-9]+\.[0-9]+\.[0-9]+/) {
          v = substr($0, RSTART, RLENGTH); sub(/ref:[[:space:]]*/, "", v); ref_v = v; ref_ln = NR
        }
        END { flush() }
      ' "$f"
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
  local root="$1" pins versions n
  pins="$(scan "$root")"
  if [ -z "$pins" ]; then
    echo "check-pins: no SciForge pins found under $root"
    return 0
  fi
  versions="$(printf '%s\n' "$pins" | cut -f1 | sort -u)"
  n="$(printf '%s\n' "$versions" | grep -c .)"
  if [ "$n" -le 1 ]; then
    echo "check-pins: OK — SciForge pinned at $versions across $(printf '%s\n' "$pins" | grep -c .) site(s) under $root"
    return 0
  fi
  echo "check-pins: DRIFT — $n distinct SciForge versions pinned:"
  printf '%s\n' "$pins" | sort | while IFS=$'\t' read -r v loc form; do
    printf '  %-14s %s (%s)\n' "$v" "$loc" "$form"
  done
  return 1
}

# Each drift case pins v1.0.0 through a `uses:` line and ONE other form at v2.0.0, so exactly that form
# can produce the drift, and the verdict must name it at v2.0.0. A form that stopped being read leaves
# its case green, which fails the self-test.
self_test() {
  local tmp failures=0 base
  tmp="$(mktemp -d)"
  base='jobs:
  build:
    uses: RECHE23/sciforge/.github/workflows/build-cpp.yml@v1.0.0
'
  expect() { # expect <name> <ok|drift> <form named at v2.0.0, or empty> [<path> <content>]...
    local name="$1" want="$2" form="$3" out rc
    shift 3
    rm -rf "$tmp/repo"
    mkdir -p "$tmp/repo/.github/workflows" "$tmp/repo/cmake"
    while [ "$#" -gt 0 ]; do printf '%s' "$2" > "$tmp/repo/$1"; shift 2; done
    set +e
    out="$(check "$tmp/repo")"
    rc=$?
    set -e
    if { [ "$want" = ok ] && [ "$rc" -ne 0 ]; } || { [ "$want" = drift ] && [ "$rc" -ne 1 ]; } \
       || { [ -n "$form" ] && ! printf '%s' "$out" | grep -qE "v2\.0\.0 .*\($form\)"; }; then
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
  expect "uses: drift" drift uses \
    .github/workflows/ci.yml "$base" .github/workflows/other.yml 'jobs:
  x:
    uses: RECHE23/sciforge/.github/workflows/lint-cpp.yml@v2.0.0
'
  expect "sciforge-ref drift" drift sciforge-ref \
    .github/workflows/ci.yml "$base    with:
      sciforge-ref: v2.0.0
"
  expect "checkout, ref after repository" drift checkout \
    .github/workflows/ci.yml "$base  lint:
    steps:
      - uses: actions/checkout@v6
        with:
          repository: RECHE23/sciforge
          ref: v2.0.0
"
  expect "checkout, ref before repository" drift checkout \
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
      - uses: actions/checkout@v6
        with:
          repository: someone/else
          ref: v2.0.0
"
  expect "CMake tag drift (top-level list file)" drift cmake \
    .github/workflows/ci.yml "$base" CMakeLists.txt 'set(SCIFORGE_TAG "v2.0.0" CACHE STRING "tag")
'
  expect "CMake tag drift (cmake/ module)" drift cmake \
    .github/workflows/ci.yml "$base" cmake/deps.cmake 'set(SCIFORGE_TAG v2.0.0)
'
  rm -rf "$tmp"
  if [ "$failures" -ne 0 ]; then
    echo "check-pins: self-test FAILED ($failures case(s))"
    return 1
  fi
  echo "check-pins: self-test OK — uses, sciforge-ref, checkout (either order), CMake list file and module each drift on their own; a foreign checkout's ref is not attributed"
}

if [ "${1:-}" = "--self-test" ]; then
  self_test
else
  check "${1:-.}"
fi
