# SciForge

Shared infrastructure for the scientific-computing ecosystem (REAL, SciLex,
SciParse, SciLang, SciNum). v0 ships one asset: the canonical C++ test harness
at `<sciforge/test/framework.hpp>`, exposed as the header-only CMake target
`sciforge::test`. Consumers add `include/` to their test target's include path
(sibling checkout by default; FetchContent for reproducible builds).

`make test` builds and runs the self-tests (a passing smoke test plus a
`WILL_FAIL` meta test that proves the harness detects failures).
