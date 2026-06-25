# SciForge

Shared infrastructure for the scientific-computing ecosystem (REAL, SciLex,
SciParse, SciLang, SciNum): the canonical C++ test harness at
`<sciforge/test/framework.hpp>` (the header-only CMake target `sciforge::test`)
and the shared lint configuration in `lint/`. SciForge is header-only
infrastructure — it is **not** a runtime dependency and is never published to
PyPI.

`make test` builds and runs the self-tests (a passing smoke test plus a
`WILL_FAIL` meta test that proves the harness detects failures).

## Consuming SciForge

The harness is consumed two ways; both put `include/` on the test target's
include path, so the headers are reached as `<sciforge/test/framework.hpp>`.

**1. Sibling checkout (local development).** Point a cache variable at a sibling
clone and wire it onto the test binary:

```cmake
set(SCIFORGE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sciforge/include"
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
> end to end. The reusable content is still deliberately minimal; it grows in
> later slices.

## Shared lint configuration

SciForge also owns the lint configuration shared by every ecosystem repository,
in `lint/`:

- **`lint/clang-tidy-misra`** — the MISRA C++:2023-oriented profile (the *base*).
  A consumer's `make misra` points clang-tidy at it with `--config-file`, supplies
  its own `--header-filter`/synthetic TU, and — if it has one extra justified
  deviation — appends it on the command line with `--checks=…` rather than forking
  the file. Each consumer documents its deviations in its own `MISRA.md`.
- **`lint/uncrustify.cfg`** — the formatter config, used verbatim by every repo's
  `make format` / `format-check`.

`make lint-config` self-tests the base (`lint/test/`): it must parse and still
behave — an enabled check fires, the documented deviations stay suppressed — so a
future edit can't silently break a consumer. The check is behavioural (not
`--verify-config`-strict) because the base deliberately disables checks that exist
in only one clang-tidy version.

Resolve the path the same way as the headers — a sibling `../sciforge/lint` locally,
or the pinned FetchContent checkout in CI.

## License

MIT — see [LICENSE](LICENSE).
