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

.PHONY: all build test format format-check lint lint-config binding-selftest-gpp bench-selftest bench-cpp-selftest clean release help

PYTHON ?= python3

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
	@echo "  make binding-selftest-gpp  Build+run the binding fixture under g++ (clang/g++ divergence gate)"
	@echo "  make bench-selftest  Run the sciforge.bench substrate selftests (stats + schema)"
	@echo "  make bench-cpp-selftest  Build the C++ collector selftest (clang+g++) + Python round-trip"
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

# Build + run the binding selftest fixture under g++ (not just clang), against the
# working-tree headers, so clang/g++ divergences on the template substrate are caught
# locally instead of only at the CI ubuntu-g++ leg. (S1 shipped a g++-only macro error that
# the clang-only local gate missed; this closes that hole.) Prefers g++-14, then g++-13.
binding-selftest-gpp:
	@gpp=$$(command -v g++-14 || command -v g++-13 || command -v g++); \
	 if [ -z "$$gpp" ]; then echo "binding-selftest-gpp: no g++ found (install g++-14/g++-13)"; exit 1; fi; \
	 pyinc=$$($(PYTHON) -c 'import sysconfig; print(sysconfig.get_path("include"))'); \
	 if [ "$$(uname -s)" = "Darwin" ]; then link="-bundle -undefined dynamic_lookup"; else link="-shared"; fi; \
	 ext=examples/binding_consumer/bindingdemo/_demo.abi3.so; \
	 echo "g++ selftest: $$gpp ($$(uname -s))"; \
	 $$gpp -std=c++20 -O2 -fPIC $$link -Wall -Wextra -DPy_LIMITED_API=0x030A0000 \
	   -Iinclude -I$$pyinc examples/binding_consumer/bindingdemo/_demo.cpp -o $$ext && \
	 PYTHONPATH=examples/binding_consumer $(PYTHON) -m unittest discover -s examples/binding_consumer/python/tests; \
	 status=$$?; rm -f $$ext; exit $$status

# Selftest the dev-only benchmark substrate (python/sciforge/bench): pure-stdlib stats and
# the exchange schema. Selftest-first — the substrate is proven here before any consumer
# (real-regex, scinum) leans on it. PYTHONPATH=python makes `import sciforge.bench` resolve
# to the in-tree package (the same sibling layout consumers use via ../sciforge/python).
bench-selftest:
	PYTHONPATH=python $(PYTHON) -m unittest discover -s python/sciforge/bench/tests -p 'test_*.py'

# Selftest the C++ raw collector (include/sciforge/bench.hpp). Compiles tests/bench_emit.cpp
# under clang AND g++ (closing the clang/g++ gap on the templates, like binding-selftest-gpp),
# runs the in-process C++ assertions, and pipes the emitted Run JSON into the Python checker
# (tests/bench_roundtrip.py reads it back with sciforge.bench.run_from_json) — proving the
# exact schema.run_to_json contract. The g++ leg is best-effort (skipped when absent).
bench-cpp-selftest:
	@mkdir -p $(BUILD)
	@gpp=$$(command -v g++-14 || command -v g++-13 || command -v g++ || true); \
	 status=0; \
	 for cxx in $${CXX:-c++} $$gpp; do \
	   [ -z "$$cxx" ] && continue; \
	   echo "bench-cpp-selftest: $$cxx"; \
	   "$$cxx" -std=c++20 -O2 -Iinclude tests/bench_emit.cpp -o $(BUILD)/bench_emit || { status=1; break; }; \
	   if $(BUILD)/bench_emit > $(BUILD)/bench_emit.out 2>&1; then cat $(BUILD)/bench_emit.out; \
	   else echo "  C++ asserts FAILED:"; cat $(BUILD)/bench_emit.out; status=1; break; fi; \
	   PYTHONPATH=python $(PYTHON) tests/bench_roundtrip.py < $(BUILD)/bench_emit.out || { status=1; break; }; \
	 done; \
	 rm -f $(BUILD)/bench_emit $(BUILD)/bench_emit.out; \
	 if [ $$status -eq 0 ]; then echo "bench-cpp-selftest: OK"; else echo "bench-cpp-selftest: FAIL"; exit 1; fi

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
