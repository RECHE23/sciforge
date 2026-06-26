# Build system for SciForge — the ecosystem's shared infrastructure repository.
# This v0 slice ships exactly one asset: the canonical C++ test harness
# (include/sciforge/test/framework.hpp), exposed as the header-only INTERFACE
# target sciforge::test. The root Makefile is a thin orchestrator over these
# targets. Reserved for later slices (build, abi3 packaging, substrat): see the
# proposal — not created here to keep the tree honest about what exists.
cmake_minimum_required(VERSION 3.16)
project(sciforge LANGUAGES CXX)

# Use ccache as a compiler launcher when available (a no-op otherwise). Override
# with -DUSE_CCACHE=OFF.
option(USE_CCACHE "Use ccache as the compiler launcher when found" ON)
if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

# Header-only test harness: consumers link sciforge::test to get the include
# path, then #include <sciforge/test/framework.hpp>.
add_library(sciforge_test INTERFACE)
add_library(sciforge::test ALIAS sciforge_test)
target_include_directories(sciforge_test INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(sciforge_test INTERFACE cxx_std_20)

# Header-only binding substrate: consumers link sciforge::binding to get the include
# path, then #include <sciforge/binding/error.hpp> / <.../gil.hpp>. Unlike the
# dev-only test harness, this is a *substrate* (it compiles into each binding's abi3
# .so); Python builds reach the same headers at build time via the build-time-only
# `sciforge-build` package (python/sciforge-build/, sciforge_build.get_include()).
add_library(sciforge_binding INTERFACE)
add_library(sciforge::binding ALIAS sciforge_binding)
target_include_directories(sciforge_binding INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(sciforge_binding INTERFACE cxx_std_17)

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Tests build only when SciForge is the top-level project (a consumer that pulls
# this in via add_subdirectory/FetchContent does not build our self-tests).
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

enable_testing()

# The harness owns no production code, so its own correctness is proven by two
# tiny self-tests rather than by a downstream consumer alone:
#   * smoke — a true assertion passes (the harness compiles, links, runs).
#   * meta  — a deliberately false assertion; the binary exits non-zero, and
#             CTest's WILL_FAIL turns that into a PASS. This proves the harness
#             genuinely detects failures, so the smoke green is not vacuous.
# main.cpp (the runner) is linked into each so the registry holds only that
# binary's test.
add_executable(sciforge_test_smoke tests/main.cpp tests/test_smoke.cpp)
add_executable(sciforge_test_meta tests/main.cpp tests/test_meta.cpp)
foreach(target sciforge_test_smoke sciforge_test_meta)
    target_link_libraries(${target} PRIVATE sciforge::test)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /Zc:__cplusplus /EHsc /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

add_test(NAME smoke COMMAND $<TARGET_FILE:sciforge_test_smoke>)
add_test(NAME meta COMMAND $<TARGET_FILE:sciforge_test_meta>)
# The meta binary is supposed to fail: a non-zero exit is the expected result.
set_tests_properties(meta PROPERTIES WILL_FAIL TRUE)
