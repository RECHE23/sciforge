# SciForge

Shared infrastructure for the scientific-computing ecosystem (REAL, SciLex,
SciParse, SciLang, SciNum). v0 ships one asset: the canonical C++ test harness
at `<sciforge/test/framework.hpp>`, exposed as the header-only CMake target
`sciforge::test`. SciForge is header-only infrastructure — it is **not** a
runtime dependency and is never published to PyPI.

`make test` builds and runs the self-tests (a passing smoke test plus a
`WILL_FAIL` meta test that proves the harness detects failures).

## Consuming SciForge

The harness is consumed two ways; both put `include/` on the test target's
include path, so the headers are reached as `<sciforge/test/framework.hpp>`.

**1. Sibling checkout (local development).** Point a cache variable at a sibling
clone and wire it onto the test binary:

```cmake
set(SCIFORGE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sciforge-v1/include"
    CACHE PATH "Path to SciForge's include directory")
target_include_directories(my_tests PRIVATE "${SCIFORGE_INCLUDE_DIR}")
```

Uncommitted cross-repo edits are visible immediately — handy while co-developing
the harness and its consumers.

**2. FetchContent + pinned tag (CI / reproducible builds).** Pull a pinned
CalVer tag from the remote, no on-disk-layout assumption:

```cmake
include(FetchContent)
FetchContent_Declare(sciforge
    GIT_REPOSITORY https://github.com/RECHE23/sciforge.git
    GIT_TAG        v2026.6.0)
FetchContent_MakeAvailable(sciforge)
target_link_libraries(my_tests PRIVATE sciforge::test)
```

A worked external example lives in `examples/dummy_consumer/` (it uses
`SOURCE_DIR` instead of a remote so it builds against the local tree).

> **`v2026.6.0` is the bootstrap milestone:** it proves the inclusion mechanism
> end to end. The reusable content is still deliberately minimal (one test
> harness); it grows in later slices.

## License

MIT — see [LICENSE](LICENSE).
