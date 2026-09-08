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
# The Doxygen this image's apt gives, and the version the consuming repo's publishing workflows
# assert. One number, two places, and neither may move alone.
want_version="1.9.8"

if ! command -v docker >/dev/null 2>&1; then
  echo "doxygen-check: docker not found (needed to run the exact CI Doxygen)" >&2
  exit 2
fi

# Mount the repo read-only; redirect Doxygen's output to a writable container path so nothing is written
# back into the tree. INPUT paths in the Doxyfile stay relative to the repo root (-w /src).
exec docker run --rm -v "$project_dir":/src:ro -w /src -e DOXYFILE="$doxyfile" -e IMAGE="$image" -e WANT="$want_version" "$image" sh -euc '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >/dev/null 2>&1
  apt-get install -y -qq doxygen graphviz >/dev/null 2>&1
  version=$(doxygen --version)
  # The version is ASSERTED, not merely reported. This script says "the CI version" in its own
  # output, so a consuming repo whose publishing workflows assert the same number has a second owner
  # of it -- and if the image package moves while only one side notices, a developer stays green
  # locally while publication goes red, or the reverse. Parsing is the first whitespace-separated
  # field, which is what such an assertion must also use: the apt binary prints a bare "1.9.8" while
  # the package is 1.9.8+ds-2ubuntu0.1, so the PACKAGE version is the wrong thing to compare.
  got=$(doxygen --version | cut -d" " -f1)
  if [ "$got" != "$WANT" ]; then
    echo "doxygen-check: Doxygen $got in $IMAGE, expected $WANT. This script is one owner of that" >&2
    echo "  number; a consuming repo whose publishing workflows assert it is the other. The image" >&2
    echo "  package moved: re-pin BOTH ends deliberately, or accept the new version in both. Naming" >&2
    echo "  a consumer file here would be wrong -- this tool does not know its consumers." >&2
    exit 1
  fi
  if ( cat "$DOXYFILE"; echo "OUTPUT_DIRECTORY=/tmp/doxout" ) | doxygen - ; then
    echo "doxygen-check: clean under Doxygen $version (the CI version)"
  else
    echo "doxygen-check: FAILED under Doxygen $version (the CI version) — fix the warnings above" >&2
    exit 1
  fi
'
