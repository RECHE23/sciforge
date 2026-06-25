# SciForge test-harness migration — source of truth

The canonical test harness lives once, here, at
`include/sciforge/test/framework.hpp` (CMake target `sciforge::test`). Each
ecosystem consumer includes it as `<sciforge/test/framework.hpp>` and resolves
`SCIFORGE_INCLUDE_DIR` (sibling checkout by default, FetchContent for CI),
mirroring the pattern proven in REAL (Slice 1).

## Status

| Consumer    | State                | Notes                                                        |
|-------------|----------------------|--------------------------------------------------------------|
| real-v1     | migrated ✓           | Slice 1 (`29fcd1f`, local reserve). 1st consumer.            |
| scilex-v1   | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |
| sciparse-v1 | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |
| scilang-v1  | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |
| scinum-v1   | migrated ✓           | Slice 2. Body byte-identical (guard-only diff).             |

State legend: **migrated ✓** = consumes SciForge, local copy removed, gate green.
**sibling-coupled** = wired but not yet gated. **divergent-skip** = body differs
from canonical; left on its own copy, see § Known divergences.

## Known divergences

None. All four Slice-2 consumers had a `framework.hpp` whose functional body was
byte-identical to the canonical copy; the only difference was the include-guard
region (`#ifndef/#define`+`#endif` vs `#pragma once`), exactly as for REAL.

## Coupling caveat (temporary)

While SciForge is a LOCAL repo with no remote, all five consumers carry a reserve
coupled to this on-disk copy. Any change to `sciforge/test/framework.hpp` must be
propagated (re-gate every consumer) before their gates are considered
representative. This is resolved by remote-ising SciForge + CI FetchContent,
which should follow Slice 2 promptly.
