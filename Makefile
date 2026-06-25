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

FORMAT_FILES := $(shell find include tests -name '*.hpp' -o -name '*.cpp')

.PHONY: all build test format format-check lint clean help

.DEFAULT_GOAL := help

help:
	@echo "SciForge — build orchestrator (CMake + QA tools)"
	@echo ""
	@echo "  make build         Configure and build the self-test binaries (CMake)"
	@echo "  make test          Build and run the self-tests (ctest: smoke + meta)"
	@echo "  make format        Uncrustify, in place"
	@echo "  make format-check  Uncrustify, dry-run, exits non-zero on diff"
	@echo "  make lint          clang-tidy over the test sources"
	@echo "  make clean         Remove build artifacts"
	@echo ""
	@echo "  Override the compiler: make test CXX=g++-14"

all: build

build:
	$(CMAKE) -S . -B $(BUILD) $(CMAKE_CXX) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD) --parallel $(JOBS)

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure

format:
	uncrustify -c uncrustify.cfg --replace --no-backup $(FORMAT_FILES)

format-check:
	uncrustify -c uncrustify.cfg --check $(FORMAT_FILES)

lint:
	@ls tests/*.cpp | xargs -P $(JOBS) -I{} clang-tidy {} -- -std=c++20 -Iinclude

clean:
	rm -rf $(BUILD)
