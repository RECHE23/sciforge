#!/usr/bin/env sh
# doxygen-check.sh — build a repo's docs under the EXACT Doxygen the CI uses (1.9.8, on ubuntu:24.04),
# so a warning that a developer's newer, more permissive local Doxygen silently tolerates cannot reach
# CI (or a release). The repo's own Doxyfile already sets WARN_AS_ERROR, so Doxygen exits nonzero on any
# warning; this just reproduces that toolchain in a container.
#
# Usage:  doxygen-check.sh [PROJECT_DIR] [DOXYFILE]     (defaults: "." and "Doxyfile")
#
# Requires Docker. This script hard-fails (exit 2) if Docker is absent; a caller that must stay green
# without Docker checks `command -v docker` itself first (see the `doc-check` Makefile targets, which
# skip-with-a-warning — visible, never a false green).
set -eu

project_dir="${1:-.}"
doxyfile="${2:-Doxyfile}"
image="ubuntu:24.04"

if ! command -v docker >/dev/null 2>&1; then
  echo "doxygen-check: docker not found (needed to run the exact CI Doxygen)" >&2
  exit 2
fi

# Mount the repo read-only; redirect Doxygen's output to a writable container path so nothing is written
# back into the tree. INPUT paths in the Doxyfile stay relative to the repo root (-w /src).
exec docker run --rm -v "$project_dir":/src:ro -w /src -e DOXYFILE="$doxyfile" "$image" sh -euc '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >/dev/null 2>&1
  apt-get install -y -qq doxygen graphviz >/dev/null 2>&1
  version=$(doxygen --version)
  if ( cat "$DOXYFILE"; echo "OUTPUT_DIRECTORY=/tmp/doxout" ) | doxygen - ; then
    echo "doxygen-check: clean under Doxygen $version (the CI version)"
  else
    echo "doxygen-check: FAILED under Doxygen $version (the CI version) — fix the warnings above" >&2
    exit 1
  fi
'
