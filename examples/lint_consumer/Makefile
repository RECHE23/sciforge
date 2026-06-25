# Buildable leaf fixture used by ci-selftest.yml to exercise SciForge's reusable
# lint-cpp + build-cpp workflows against the shared lint/ config and the standard
# Make/CMake targets — without touching a real consumer's CI. Mirrors the cat-A/B
# recipe: the lint config comes from SciForge's lint/, the build is plain CMake.
SCIFORGE_LINT ?= ../../lint
CXXSTD       := -std=c++20
INCLUDES     := -Iinclude
BUILD        := build
CMAKE        ?= cmake
CTEST        ?= ctest
FORMAT_FILES := $(shell find include tests cmake -name '*.hpp' -o -name '*.cpp')

.PHONY: build test sanitize misra format-check
# CXX is honoured (passed to CMake) so the build-cpp self-test exercises both the
# g++-14 and clang++ posix matrix legs, like a real consumer's Makefile.
build:
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX)
	$(CMAKE) --build $(BUILD)

test: build
	$(CTEST) --test-dir $(BUILD) --output-on-failure

sanitize:
	$(CMAKE) -S . -B $(BUILD)/sanitize -DLINTDEMO_SANITIZE=ON -DCMAKE_CXX_COMPILER=$(CXX)
	$(CMAKE) --build $(BUILD)/sanitize
	$(CTEST) --test-dir $(BUILD)/sanitize --output-on-failure

misra:
	@mkdir -p $(BUILD)
	printf '#include <lintdemo/lintdemo.hpp>\nint main(){ return lintdemo::answer(); }\n' > $(BUILD)/misra_tu.cpp
	clang-tidy --config-file=$(SCIFORGE_LINT)/clang-tidy-misra \
	    --header-filter='include/lintdemo/.*' \
	    $(BUILD)/misra_tu.cpp -- $(CXXSTD) $(INCLUDES)

format-check:
	uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --check $(FORMAT_FILES)
