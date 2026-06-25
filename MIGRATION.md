# SciForge test-harness migration — source of truth

The canonical test harness lives once, here, at
`include/sciforge/test/framework.hpp` (CMake target `sciforge::test`). Each
ecosystem consumer includes it as `<sciforge/test/framework.hpp>` and resolves
`SCIFORGE_INCLUDE_DIR` (sibling checkout by default, FetchContent for CI),
mirroring the pattern proven in REAL (Slice 1).

## Status

| Consumer    | State                | Notes                                                        |
|-------------|----------------------|--------------------------------------------------------------|
| real-regex     | **remote-wired ✓**   | CI fetches RECHE23/sciforge@v2026.6.0 (`bde48a2`, pushed, CI+Docs green 2026-06-25). Canary. |
| scilex   | **remote-wired ✓**   | CI fetches RECHE23/sciforge@v2026.6.0 (`4a5f73f`, pushed, CI+Docs green 2026-06-25). |
| sciparse | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |
| scilang  | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |
| scinum   | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |

State legend: **remote-wired ✓** = CI fetches SciForge from the remote tag, reserve
pushed, CI green (fully decoupled). **migrated ✓** = consumes SciForge, local copy
removed, local gate green (CI still sibling-coupled — remote-wire pending).
**divergent-skip** = body differs from canonical; left on its own copy.

## Known divergences

None. All four Slice-2 consumers had a `framework.hpp` whose functional body was
byte-identical to the canonical copy; the only difference was the include-guard
region (`#ifndef/#define`+`#endif` vs `#pragma once`), exactly as for REAL.

## Remote — `RECHE23/sciforge`, v2026.6.0 (public)

SciForge is now a public remote repository (`RECHE23/sciforge`), CI-validated on
all three OS (incl. the dummy-consumer proving FetchContent works in CI), and
released at the first CalVer tag **v2026.6.0** — a pinnable tag. v2026.6.0 is a
bootstrap milestone: it proves the inclusion mechanism; the only reusable asset so
far is the canonical test harness.

## Coupling caveat (being resolved)

Each consumer currently carries a local reserve that resolves the harness via the
SIBLING `SCIFORGE_INCLUDE_DIR` — their CI cannot yet find it. The next slice wires
each consumer's CI to FetchContent `RECHE23/sciforge` at `SCIFORGE_TAG=v2026.6.0`
and pushes its reserve, which decouples all six reserves at once. Until then, any
change to `sciforge/test/framework.hpp` must be re-gated across consumers.
