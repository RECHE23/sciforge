# Thin orchestrator over CMake (build/test) and the formatting/lint tools. CMake
# owns all compilation policy (CMakeLists.txt); this file only wires the frequent
# commands. Override the compiler with CXX on the command line, e.g.
#   make test CXX=g++-14
# Switching compilers reuses a cached build dir, so run `make clean` first.

CMAKE ?= cmake
CTEST ?= ctest
BUILD := build
# Parallelism: detected core count (override with JOBS=N).
JOBS  ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Forward CMAKE_CXX_COMPILER only when CXX is set on the command line; otherwise
# CMake selects the platform default.
ifeq ($(origin CXX),command line)
CMAKE_CXX := -DCMAKE_CXX_COMPILER=$(CXX)
endif

# Prune build/ dirs: the lint_consumer fixture generates a TU under build/ (and
# CMake builds land there too) — those are not ours to format.
FORMAT_FILES := $(shell find include tests examples -name build -prune -o \( -name '*.hpp' -o -name '*.cpp' \) -print)

.PHONY: all build test format format-check lint lint-config clean release help

.DEFAULT_GOAL := help

help:
	@echo "SciForge — build orchestrator (CMake + QA tools)"
	@echo ""
	@echo "  make build         Configure and build the self-test binaries (CMake)"
	@echo "  make test          Build and run the self-tests (ctest: smoke + meta)"
	@echo "  make format        Uncrustify, in place"
	@echo "  make format-check  Uncrustify, dry-run, exits non-zero on diff"
	@echo "  make lint          clang-tidy over the test sources"
	@echo "  make lint-config   Self-test the shared MISRA base (lint/clang-tidy-misra)"
	@echo "  make clean         Remove build artifacts"
	@echo "  make release       Tag a calendar-versioned release and push (no PyPI)"
	@echo "                     dry-run: make release DRY_RUN=1"
	@echo ""
	@echo "  Override the compiler: make test CXX=g++-14"

all: build

build:
	$(CMAKE) -S . -B $(BUILD) $(CMAKE_CXX) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD) --parallel $(JOBS)

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure

format:
	uncrustify -c lint/uncrustify.cfg --replace --no-backup $(FORMAT_FILES)

format-check:
	uncrustify -c lint/uncrustify.cfg --check $(FORMAT_FILES)

lint:
	@ls tests/*.cpp | xargs -P $(JOBS) -I{} clang-tidy {} -- -std=c++20 -Iinclude

# Self-test the shared MISRA base that SciForge ships for the whole ecosystem
# (lint/clang-tidy-misra): it must parse and still behave (an enabled check fires,
# the documented deviations stay suppressed). Override the binary with CLANG_TIDY=.
lint-config:
	@CLANG_TIDY=$${CLANG_TIDY:-clang-tidy} lint/test/run.sh

clean:
	rm -rf $(BUILD)

# Cut a calendar-versioned release: vYYYY.M.PATCH where PATCH is the count of tags
# already cut this calendar month (so the first ever is v2026.6.0). SciForge is
# header-only infrastructure — a release is a git tag + push, nothing more (no
# version bump, no PyPI). Hard guards refuse anything unsafe; `DRY_RUN=1` computes
# and prints the version without touching git or the remote (works before the
# repo has an 'origin', i.e. during local bring-up).
release:
	@branch=$$(git symbolic-ref --short HEAD 2>/dev/null); \
	  test "$$branch" = main || { echo "release: must be on main (on '$$branch')"; exit 1; }
	@test -z "$$(git status --porcelain)" || { echo "release: working tree not clean"; exit 1; }
	@if [ -z "$(DRY_RUN)" ]; then \
	   git remote get-url origin >/dev/null 2>&1 || { echo "release: no 'origin' remote"; exit 1; }; \
	   git fetch --tags --quiet origin; \
	 fi
	@year=$$(date -u +%Y); month=$$(date -u +%m | sed 's/^0//'); \
	  patch=$$(git tag -l "v$$year.$$month.*" | wc -l | tr -d ' '); \
	  version="v$$year.$$month.$$patch"; \
	  if git rev-parse -q --verify "refs/tags/$$version" >/dev/null; then \
	    echo "release: tag $$version already exists"; exit 1; fi; \
	  if [ -n "$(DRY_RUN)" ]; then echo "[dry-run] would tag and push $$version"; exit 0; fi; \
	  echo "Releasing $$version"; \
	  git tag "$$version"; \
	  git push origin "$$version"
