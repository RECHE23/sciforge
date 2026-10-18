# SciForge conventions

The shared conventions the ecosystem repos (SciForge, REAL, SciLex) follow. They are recorded here so
a new file, or a new repo, is consistent by construction rather than by memory.

## Header guards — include guards, not `#pragma once`

Every header opens with a named include guard and closes with a matching `#endif`; `#pragma once` is
**not** used. The guard name is the path under `include/`, uppercased with non-alphanumerics turned to
`_`:

```cpp
// include/sciforge/test/strings.hpp
#ifndef SCIFORGE_TEST_STRINGS_HPP
#define SCIFORGE_TEST_STRINGS_HPP
// ...
#endif // SCIFORGE_TEST_STRINGS_HPP
```

Include guards are portable to every toolchain (no reliance on a compiler recognising the same file
across symlinks / build layouts) and are greppable, which the purity and doc tooling rely on.

## Naming

- **Namespaces / types / functions / variables:** `lower_snake_case`. Template parameters are
  `CamelCase`. Macros (kept rare) are `UPPER_SNAKE_CASE` and always prefixed with the repo (`SCIFORGE_`,
  `REAL_`, `SCILEX_`).
- **Files:** `lower_snake_case.hpp` / `.cpp`, mirroring the namespace path.

## Documentation canon, by component type

Doxygen throughout. Each component type has a canonical shape:

- **Public API (a class / free function):** a `\brief`, then `\param` / `\return` / `\throws` as they
  apply. State the contract, not the implementation.
- **A header (`\file`):** one paragraph on what the header is for and its one key idea.
- **A design/tour page (`.dox`):** narrative, from the visible surface inward.
- **An enum / reserved constant:** one line each on meaning and, where relevant, its numeric slot.

## Purity of the delivered tree

No development-process codes (arc/slice/batch names, ticket ids, "the fiche", reviewer names) appear in
**delivered** files — code, public docs, tests, bindings. Reasons are given in domain terms; the
process genealogy lives in commit messages only. A grep for process codes over the delivered tree is
expected to be empty.

## Docs: the CI-Doxygen check (`doc-check`)

A developer's local Doxygen is usually newer and more permissive than the one CI runs, so a warning can
pass locally and fail the Docs build. Each repo has a `doc-check` target that runs the docs under the
**exact CI Doxygen (1.9.8, in Docker)** via `tools/doxygen-check.sh`, wired into `full-local-gate` and
skipped with a warning when Docker is absent (never a false green). Run it before publishing any
doc-touching change.

## Pinning the reusable workflows

REAL and SciLex consume SciForge's reusable CI workflows (`lint-cpp.yml`, `build-cpp.yml`,
`python-cpp.yml`) and its headers. Both are pinned to the **same CalVer tag** across a repo's workflow
file — the `uses: …@vX` workflow reference and the `sciforge-ref:` / `ref:` checkout — so a job never
mixes SciForge versions. Direct `actions/checkout` steps (coverage, sanitize, docs jobs that run `make`
themselves) take the same tag as the reusable-workflow jobs. Bump all of them together. The CMake fetch tag
(`set(SCIFORGE_TAG "vX" …)` in `CMakeLists.txt` or a `cmake/` module) is the same pin in a fourth
place and moves with them. `tools/check-pins.sh <repo>` reads all four and fails on more than one
version; `--self-test` proves each form is read.

## Verification tiers (`make/quality.mk`)

Every project verifies its changes in three tiers, from the same mechanism: `include $(SCIFORGE)/make/quality.mk`
in its Makefile, after listing its own make targets in three variables.

- `make check-commit` — before each commit (`QUALITY_COMMIT`): the quick checks, a couple of minutes at most —
  formatting, the test suite under coverage (`coverage-gate`, 100 % on every dimension), what the commit cannot
  break unseen.
- `make check-push` — before each push: the commit tier, then `QUALITY_PUSH` — every compiler, the sanitizers,
  lint, MISRA, the documentation build, the bindings' suites.
- `make check-release` — before each release: both, then `QUALITY_RELEASE` — benchmarks against the last release,
  long fuzzing campaigns, the external oracles.

A step is a make target (one that needs arguments is a target of its own, so that `make <step>` reproduces it).
The tiers run fail-fast and print each step's time; they take one lock per tree and refuse a second gate while the
first lives, and a gate whose `HEAD` or working tree moved while it ran reports its verdict void. A check moves
between tiers when its cost or its catch rate says so; none leaves every tier without the maintainer's word.

## Versioning and the `sciforge-build` floor

- SciForge follows **CalVer** (`vYEAR.MONTH.PATCH`; the patch resets each month). A tag is infra-only
  unless it changes a published artefact.
- Consumers pin the `sciforge-build` backend with a **floor, never an exact pin**: `sciforge-build>=X`,
  not `==X`. A floor lets a consumer pick up a compatible fix without a lockstep release; SciForge keeps
  the build backend backward-compatible within a CalVer line.
- **A contract change takes a new name.** What an existing name means — a binding's return convention, a
  macro's expansion, a field of the bench schema — does not change within a CalVer line: the new behaviour
  gets a new name beside the old. Where that is impossible, CHANGELOG.md lists it under **Contract changes**
  for that tag, and every consumer raises its floor past it in the same train.
- **Generated tables / files** (a repo's `gen_*` scripts) are regenerated only by their script, never
  edited by hand; a regen that changes any output is a call-out in the release notes, not a silent bump.
