# quality.mk — the three verification tiers of a SciForge project. Include it from the project's Makefile
# (`include $(SCIFORGE)/make/quality.mk`, SCIFORGE the sibling checkout) after setting what each tier runs:
#
#   QUALITY_COMMIT   before each commit: the quick checks (a couple of minutes)
#   QUALITY_PUSH     before each push: added to QUALITY_COMMIT (every compiler, sanitizers, lint, MISRA, docs…)
#   QUALITY_RELEASE  before each release: added to both (benchmarks against the last release, long fuzzing…)
#
# Each is a list of the project's make targets (a step that needs arguments is a target of its own, so that
# `make <step>` reproduces it alone). `make check-commit`, `check-push` and `check-release` run them through
# tools/run-gate.sh: in order, fail-fast, timed, under one lock per tree, and void if the tree moved meanwhile.
#
# QUALITY_FIRST and QUALITY_LAST run at the start and the end of every tier (a record of the steps a tier skipped,
# reset first and judged last, say).
#
# `coverage-gate` is a step a project may list: it runs QUALITY_COVERAGE (a target that prints llvm-cov's
# report) and requires its TOTAL line at 100 % on every dimension (regions, functions, lines, branches).

QUALITY_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)

QUALITY_BUILD    ?= build
QUALITY_COMMIT   ?= format-check test
QUALITY_PUSH     ?=
QUALITY_RELEASE  ?=
QUALITY_FIRST    ?=
QUALITY_LAST     ?=
QUALITY_COVERAGE ?= coverage

QUALITY_RUN := bash $(QUALITY_DIR)/tools/run-gate.sh

.PHONY: check-commit check-push check-release coverage-gate

check-commit: ## [gates] Before each commit: QUALITY_COMMIT
	@$(QUALITY_RUN) commit $(QUALITY_BUILD) "$(MAKE)" $(QUALITY_FIRST) $(QUALITY_COMMIT) $(QUALITY_LAST)

check-push: ## [gates] Before each push: QUALITY_COMMIT, then QUALITY_PUSH
	@$(QUALITY_RUN) push $(QUALITY_BUILD) "$(MAKE)" $(QUALITY_FIRST) $(QUALITY_COMMIT) $(QUALITY_PUSH) $(QUALITY_LAST)

check-release: ## [gates] Before each release: every tier, QUALITY_RELEASE last
	@$(QUALITY_RUN) release $(QUALITY_BUILD) "$(MAKE)" $(QUALITY_FIRST) $(QUALITY_COMMIT) $(QUALITY_PUSH) $(QUALITY_RELEASE) $(QUALITY_LAST)

# The flag, not a bare exit in awk's main block: END's exit overrides it, and a gate that once did so accepted
# coverage below 100 % without a word.
coverage-gate: ## [gates] QUALITY_COVERAGE, its TOTAL at 100 % on every dimension
	@mkdir -p $(QUALITY_BUILD)
	@$(MAKE) --no-print-directory $(QUALITY_COVERAGE) > $(QUALITY_BUILD)/coverage.log 2>&1; status=$$?; \
	  cat $(QUALITY_BUILD)/coverage.log; exit $$status
	@awk '/^TOTAL/{seen=1; if (gsub(/100\.00%/, "&") != 4) bad=1} END{exit (seen && !bad) ? 0 : 1}' $(QUALITY_BUILD)/coverage.log \
	  || { echo "coverage-gate: coverage is below 100 % on some dimension (or no TOTAL line) -- see above"; exit 1; }
	@echo "coverage-gate: 100 % on every dimension"
