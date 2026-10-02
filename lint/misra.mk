# The clang-tidy that runs the shared MISRA base (clang-tidy-misra), for every consumer and for CI.
#
# One version everywhere: clang-tidy versions disagree on what they flag (18 does not follow an optional checked
# through a bool, which 22 does), so a local analysis under another version passes code CI refuses. CI's lint
# spine installs the version this file names (.github/workflows/lint-cpp.yml reads MISRA_TIDY_VERSION from here),
# and a consumer's `make misra` runs $(MISRA_TIDY) after $(MISRA_TIDY_CHECK).
#
# Consumer use, after SCIFORGE_LINT is set:
#   include $(SCIFORGE_LINT)/misra.mk
#   misra:
#   	@$(MISRA_TIDY_CHECK)
#   	$(MISRA_TIDY) --config-file=$(SCIFORGE_LINT)/clang-tidy-misra ...
#
# MISRA_TIDY takes clang-tidy-<version> on the path (Debian/Ubuntu packages, CI), else Homebrew's llvm@<version>,
# else plain clang-tidy; override it on the command line to name another binary. MISRA_TIDY_IMAGE is the
# container of that version, for an analysis under another ISA.

MISRA_TIDY_VERSION := 18
MISRA_TIDY         ?= $(or $(shell command -v clang-tidy-$(MISRA_TIDY_VERSION) 2>/dev/null),$(wildcard /opt/homebrew/opt/llvm@$(MISRA_TIDY_VERSION)/bin/clang-tidy),clang-tidy)
MISRA_TIDY_IMAGE   := silkeh/clang:$(MISRA_TIDY_VERSION)

# A warning, not a failure: a machine without the pinned version can still run the analysis, and must be told
# that a pass there may not be one in CI.
MISRA_TIDY_CHECK = $(MISRA_TIDY) --version | grep -q 'version $(MISRA_TIDY_VERSION)\.' \
                   || echo "misra -- $(MISRA_TIDY) is not clang-tidy $(MISRA_TIDY_VERSION), the version CI pins: a pass here may fail there"
