# release.mk — the calendar-versioned release every SciForge project cuts the same way (tools/release.sh). Include it
# from the project's Makefile (`include $(SCIFORGE)/make/release.mk`) after setting:
#
#   RELEASE_FILES     the files the version bump changes (besides CHANGELOG.md)
#   RELEASE_BUMP      the project's target that writes VERSION=<x> into them (default: release-bump)
#   RELEASE_PRECHECK  targets that must pass first, e.g. benchmarks against the last release (SKIP_PRECHECK=1 skips)
#   RELEASE_ADVISORY  targets run in every mode, dry run included, that warn and never block
#   RELEASE_CI        workflows that must have succeeded on HEAD, the tag being what publishes (default: CI)
#
# `make release` cuts it; `make release DRY_RUN=1` runs every guard and the bump, shows the tag body, and restores the
# tree. CHANGELOG.md's `## Unreleased` section becomes the release's; an empty one is refused.

RELEASE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)

RELEASE_FILES    ?=
RELEASE_BUMP     ?= release-bump
RELEASE_PRECHECK ?=
RELEASE_ADVISORY ?=
RELEASE_CI       ?= CI
RELEASE_REMOTE   ?= origin

.PHONY: release

release: ## [release] Cut the calendar release (DRY_RUN=1 to rehearse, SKIP_PRECHECK=1 to skip the prechecks)
	@RELEASE_MAKE="$(MAKE)" RELEASE_FILES="$(RELEASE_FILES)" RELEASE_BUMP="$(RELEASE_BUMP)" \
	 RELEASE_PRECHECK="$(RELEASE_PRECHECK)" RELEASE_ADVISORY="$(RELEASE_ADVISORY)" RELEASE_CI="$(RELEASE_CI)" RELEASE_REMOTE="$(RELEASE_REMOTE)" \
	 DRY_RUN="$(DRY_RUN)" SKIP_PRECHECK="$(SKIP_PRECHECK)" bash $(RELEASE_DIR)/tools/release.sh
