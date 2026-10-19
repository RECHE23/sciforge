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

.PHONY: all build test format format-check lint lint-binding lint-config binding-selftest-gpp bench-selftest corpus-selftest framework-selftest bench-cpp-selftest quality-selftest release-selftest pins-selftest clean release release-bump help

PYTHON ?= python3

include lint/misra.mk

# The verification tiers every SciForge project shares (make/quality.mk), SciForge's own steps in them.
QUALITY_COMMIT := format-check test quality-selftest release-selftest pins-selftest
QUALITY_PUSH   := lint lint-config framework-selftest bench-selftest corpus-selftest bench-cpp-selftest binding-selftest-gpp
include make/quality.mk

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
	@echo "  make corpus-selftest Run the sciforge.corpus substrate selftests (schema + classify)"
	@echo "  make bench-cpp-selftest  Build the C++ collector selftest (clang+g++) + Python round-trip"
	@echo "  make quality-selftest  Self-test the verification tiers (tools/run-gate.sh, make/quality.mk)"
	@echo "  make release-selftest  Self-test the shared release (tools/release.sh, make/release.mk)"
	@echo "  make check-commit  Before each commit: format, self-tests, the tiers' and the release's self-tests"
	@echo "  make check-push    Before each push: the above, lint, MISRA base, every self-test"
	@echo "  make clean         Remove build artifacts"
	@echo "  make release       Cut the calendar release: bump, commit, annotated tag, push (publishes sciforge-build)"
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
	@$(MAKE) --no-print-directory lint-binding

# The binding substrate (include/sciforge/binding/) is included by no test, so `lint` above never sees
# it. The demo consumer instantiates every header's templates; --line-filter keeps the diagnostics to
# the headers themselves, because the demo passes strings by value ON PURPOSE (it exercises the caster
# paths) and its own findings are not the substrate's. Needs the Python headers.
lint-binding:
	@pyinc=$$($(PYTHON) -c 'import sysconfig; print(sysconfig.get_path("include"))'); \
	 lf=$$($(PYTHON) -c "import glob,json; print(json.dumps([{'name': p} for p in sorted(glob.glob('include/sciforge/binding/*.hpp'))]))"); \
	 clang-tidy examples/binding_consumer/bindingdemo/_demo.cpp --line-filter="$$lf" \
	   -- -std=c++20 -Iinclude -I"$$pyinc" -DPy_LIMITED_API=0x030A0000

# Self-test the shared MISRA base that SciForge ships for the whole ecosystem
# (lint/clang-tidy-misra): it must parse and still behave (an enabled check fires,
# the documented deviations stay suppressed), under the clang-tidy lint/misra.mk pins, and that pin must be the
# only version the workflows name. Override the binary with CLANG_TIDY=.
lint-config:
	@$(MISRA_TIDY_CHECK)
	@CLANG_TIDY=$${CLANG_TIDY:-$(MISRA_TIDY)} lint/test/run.sh
	@MISRA_TIDY_VERSION=$(MISRA_TIDY_VERSION) lint/test/misra_pin.sh

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
	 conflict=examples/binding_consumer/bindingdemo/_conflict.abi3.so; \
	 throwing=examples/binding_consumer/bindingdemo/_throwing.abi3.so; \
	 $$gpp -std=c++20 -O2 -fPIC $$link -Wall -Wextra -DPy_LIMITED_API=0x030A0000 \
	   -Iinclude -I$$pyinc examples/binding_consumer/bindingdemo/_demo.cpp -o $$ext && \
	 $$gpp -std=c++20 -O2 -fPIC $$link -Wall -Wextra -DPy_LIMITED_API=0x030A0000 \
	   -Iinclude -I$$pyinc examples/binding_consumer/bindingdemo/_conflict.cpp -o $$conflict && \
	 $$gpp -std=c++20 -O2 -fPIC $$link -Wall -Wextra -DPy_LIMITED_API=0x030A0000 \
	   -Iinclude -I$$pyinc examples/binding_consumer/bindingdemo/_throwing.cpp -o $$throwing && \
	 if $$gpp -std=c++20 -fsyntax-only -DPy_LIMITED_API=0x030A0000 -Iinclude -I$$pyinc \
	      examples/binding_consumer/negative/required_after_optional.cpp 2>/dev/null; then \
	   echo "binding-selftest-gpp: a required arg() after a defaulted one compiled"; rm -f $$ext $$conflict $$throwing; exit 1; \
	 fi && \
	 PYTHONPATH=examples/binding_consumer $(PYTHON) -m unittest discover -s examples/binding_consumer/python/tests; \
	 status=$$?; rm -f $$ext $$conflict $$throwing; exit $$status

# Selftest the dev-only benchmark substrate (python/sciforge/bench): pure-stdlib stats and
# the exchange schema. Selftest-first — the substrate is proven here before any consumer
# (real-regex, scinum) leans on it. PYTHONPATH=python makes `import sciforge.bench` resolve
# to the in-tree package (the same sibling layout consumers use via ../sciforge/python).
bench-selftest:
	PYTHONPATH=python $(PYTHON) -m unittest discover -s python/sciforge/bench/tests -p 'test_*.py'

# Selftest the dev-only conformance-corpus substrate (python/sciforge/corpus): the case contract's
# machine schema and its classification rules, including the adversarial ten-trap acceptance test.
# Same sibling-layout convention as bench-selftest.
corpus-selftest:
	PYTHONPATH=python $(PYTHON) -m unittest discover -s python/sciforge/corpus/tests -p 'test_*.py'

# Meta-test the C++ test framework itself (include/sciforge/test/framework.hpp): the classic blind spot
# is a harness whose FAILED assertions are untested. A standalone child (tests/framework_selftest.cpp)
# runs one scenario per invocation; the driver asserts, per scenario, the exit code and the exact printed
# check counts — EXPECT(false) counted, EXPECT_THROWS failing when nothing throws, the success path
# clean. The framework that judges every other test earns its own oracle.
framework-selftest:
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++20 -O2 -Wall -Wextra -Wsign-compare -Werror -Iinclude tests/framework_selftest.cpp -o $(BUILD)/framework_selftest
	@$(PYTHON) tests/framework_selftest_driver.py $(BUILD)/framework_selftest

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

# Self-test the verification tiers that every project includes (tools/run-gate.sh through make/quality.mk):
# each verdict provoked on purpose in a scratch repository (tools/test/run_gate_selftest.sh).
quality-selftest:
	@bash tools/test/run_gate_selftest.sh

# Self-test the calendar release every project cuts (tools/release.sh through make/release.mk): a dry run, a release
# pushed to a bare remote, and each refusal, in a scratch repository (tools/test/release_selftest.sh).
release-selftest:
	@bash tools/test/release_selftest.sh

# Self-test the pin lint every consumer runs (tools/check-pins.sh): each form of pin, each drift, the checkout it runs
# from, in fixture repositories.
pins-selftest:
	@bash tools/check-pins.sh --self-test

clean:
	rm -rf $(BUILD)

# The calendar release (make/release.mk, the one every project cuts): vYYYY.M.PATCH, PATCH the count of the month's
# tags. Pushing the tag also publishes sciforge-build to PyPI (release.yml), whose check-version job refuses a tag that
# differs from python/sciforge-build/pyproject.toml: release-bump writes that version in the release commit, and the
# tag is cut only on a commit whose CI and CI selftest are green.
release-bump:
	@test -n "$(VERSION)" || { echo "release-bump: VERSION= is required"; exit 1; }
	@sed -i.bak -E "s/^version = \".*\"/version = \"$(VERSION)\"/" python/sciforge-build/pyproject.toml \
	  && rm -f python/sciforge-build/pyproject.toml.bak

RELEASE_FILES := python/sciforge-build/pyproject.toml
RELEASE_CI    := ci.yml ci-selftest.yml
RELEASE_CI_IGNORED := *.md
include make/release.mk
