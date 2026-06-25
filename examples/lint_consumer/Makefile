# Minimal ecosystem-consumer fixture used by ci-selftest.yml to exercise SciForge's
# reusable lint-cpp workflow (make misra + make format-check against the shared
# lint/ config) without touching a real consumer's CI. Mirrors the cat-A recipe:
# the config comes from SciForge's lint/, the header-filter + TU stay local.
SCIFORGE_LINT ?= ../../lint
CXXSTD       := -std=c++20
INCLUDES     := -Iinclude
BUILD        := build
FORMAT_FILES := $(shell find include -name '*.hpp')

.PHONY: misra format-check
misra:
	@mkdir -p $(BUILD)
	printf '#include <lintdemo/lintdemo.hpp>\nint main(){ return lintdemo::answer(); }\n' > $(BUILD)/misra_tu.cpp
	clang-tidy --config-file=$(SCIFORGE_LINT)/clang-tidy-misra \
	    --header-filter='include/lintdemo/.*' \
	    $(BUILD)/misra_tu.cpp -- $(CXXSTD) $(INCLUDES)

format-check:
	uncrustify -c $(SCIFORGE_LINT)/uncrustify.cfg --check $(FORMAT_FILES)
