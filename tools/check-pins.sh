#!/usr/bin/env bash
# check-pins.sh — fail on SciForge pin drift across a consumer repo's GitHub workflows.
#
# A consumer references SciForge three ways, and they must all name the SAME version:
#   1. a reusable-workflow pin      uses: RECHE23/sciforge/.github/workflows/X.yml@vVERSION
#   2. a reusable-workflow input    sciforge-ref: vVERSION
#   3. a direct checkout            repository: RECHE23/sciforge  +  ref: vVERSION
# The .6.5/.6/.7 drift (one file bumped, another not) shipped mismatched substrates before. This lint
# collects every pinned version and fails if more than one distinct version appears.
#
# Usage: check-pins.sh [REPO_ROOT]   (default: current directory)
set -euo pipefail

root="${1:-.}"
dir="$root/.github/workflows"
if [ ! -d "$dir" ]; then
  echo "check-pins: no $dir — nothing to check"
  exit 0
fi

# Emit "version<TAB>file:line<TAB>form" for every SciForge pin found.
scan() {
  for f in "$dir"/*.yml "$dir"/*.yaml; do
    [ -e "$f" ] || continue
    # 1. reusable-workflow uses: ...@vVERSION
    grep -nE 'RECHE23/sciforge/[^@]+@v[0-9]+\.[0-9]+\.[0-9]+' "$f" | while IFS=: read -r ln rest; do
      v=$(printf '%s' "$rest" | grep -oE '@v[0-9]+\.[0-9]+\.[0-9]+' | tr -d '@')
      printf '%s\t%s:%s\tuses\n' "$v" "$f" "$ln"
    done
    # 2. sciforge-ref: vVERSION
    grep -nE 'sciforge-ref:[[:space:]]*v[0-9]+\.[0-9]+\.[0-9]+' "$f" | while IFS=: read -r ln rest; do
      v=$(printf '%s' "$rest" | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+')
      printf '%s\t%s:%s\tsciforge-ref\n' "$v" "$f" "$ln"
    done
    # 3. checkout of RECHE23/sciforge: the ref: paired with the repository: line (either order, within
    #    the same step's with-block — a small window).
    awk -v file="$f" '
      /repository:[[:space:]]*RECHE23\/sciforge/ { repo_ln = NR }
      match($0, /ref:[[:space:]]*v[0-9]+\.[0-9]+\.[0-9]+/) {
        v = substr($0, RSTART, RLENGTH); sub(/ref:[[:space:]]*/, "", v); ref_ln = NR; ref_v = v
      }
      END { }
      {
        if (repo_ln && ref_v && (NR - repo_ln) <= 4 && ref_ln >= repo_ln) {
          printf "%s\t%s:%s\tcheckout\n", ref_v, file, ref_ln
          repo_ln = 0; ref_v = ""
        }
      }
    ' "$f"
  done
}

pins="$(scan)"
if [ -z "$pins" ]; then
  echo "check-pins: no SciForge pins found in $dir"
  exit 0
fi

versions="$(printf '%s\n' "$pins" | cut -f1 | sort -u)"
n="$(printf '%s\n' "$versions" | grep -c .)"
if [ "$n" -le 1 ]; then
  echo "check-pins: OK — SciForge pinned at $versions across $dir"
  exit 0
fi

echo "check-pins: DRIFT — $n distinct SciForge versions pinned:"
printf '%s\n' "$pins" | sort | while IFS=$'\t' read -r v loc form; do
  printf '  %-14s %s (%s)\n' "$v" "$loc" "$form"
done
exit 1
