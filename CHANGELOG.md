# Changelog

All notable changes to SciForge, by tag. Versions are CalVer (`vYEAR.MONTH.PATCH`, the patch resetting each
month). A change to what an existing name means is listed under **Contract changes** (see CONVENTIONS.md,
"Versioning"). Changes are written under `## Unreleased` as they land; `make release` files that section under the
version it cuts and refuses an empty one.

## Unreleased

## 2026.10.7 — 2026-10-30

### Added
- **The binding runs without the GIL on a free-threaded CPython** (3.14t, 3.15t: `Py_GIL_DISABLED`): `SCIFORGE_MODULE`
  declares it (`PyUnstable_Module_SetGIL(module, Py_MOD_GIL_NOT_USED)`), where the interpreter re-enabled the GIL for
  the module with a warning. The substrate writes its state at import only (the method and type tables, each
  function's keyword spec), which the import lock serialises, and reads it afterwards. A consumer builds that wheel
  without `Py_LIMITED_API` (the free-threaded headers refuse it), and declares nothing of its own unless its state is
  safe without the GIL. Tested: NumericAL's binding under CPython 3.14.6t, the GIL still disabled after the import, its
  290 tests, and eight Python threads calling 27 functions 300 times each with byte-identical answers.

## 2026.10.6 — 2026-10-20

### Added
- **`docs/adopting.md`**: how a project takes up the verification tiers (`make/quality.mk`), the calendar release
  (`make/release.mk`) and the pin check (`tools/check-pins.sh`) together, in the order it needs them, with what each
  variable is for and how a consumer raises its pin after a release.
- **Issue forms** (`.github/ISSUE_TEMPLATE/`): a bug report (version, platform, the smallest reproduction, what was
  expected), a feature request, and security issues sent to private reporting.

### Changed
- **The README's `FetchContent` example names the release that published it**: `release-bump` writes the new tag into
  its `GIT_TAG` line (`README.md` joins `RELEASE_FILES`), where it had stayed at `v2026.9.0` across five releases.

## 2026.10.5 — 2026-10-19

### Added
- **`check-pins.sh` judges the SciForge checkout it runs from**: the sibling whose `make/`, `lint/` and `tools/` a
  consumer's Makefile reads must be the version the consumer pins, exactly and unmodified, or the check fails
  (`SIBLING`), naming both and the checkout that agrees. A local gate otherwise ran what no CI run of the pins
  does, and the four consumers came to rely on `RELEASE_CI_IGNORED` before any release had it.
  `SCIFORGE_UNPINNED_OK=1` makes it a warning, to work on SciForge and a consumer together; outside a git checkout
  it is skipped; `SCIFORGE_CHECKOUT` names another checkout. The self-test drives each case (ahead, modified,
  allowed, at the pin, no history), each failing alone with its arm removed.
- **`RELEASE_CI_IGNORED`** (`make/release.mk`): the paths the release's workflows skip, as globs (their
  `paths-ignore`, such as `*.md docs/*`). A HEAD they did not run on, changed in those paths alone since their last
  run, is judged by that run: a CHANGELOG commit no longer stops a release. A workflow that did not run otherwise is
  refused with the command that runs it (`gh workflow run`).

### Changed
- **The Releases page carries the tag's annotation** (`release.yml`): the CHANGELOG section `make release` annotates the
  tag with, before a `docs/release-notes/<tag>.md` and the list of commits it fell back to.

## 2026.10.4 — 2026-10-18

### Added
- **Verification tiers every project shares** (`make/quality.mk`, `tools/run-gate.sh`): `make check-commit`,
  `check-push` and `check-release` run the make targets a project lists in `QUALITY_COMMIT`, `QUALITY_PUSH` and
  `QUALITY_RELEASE` (each tier adding to the one before), in order, fail-fast, each step timed, under one lock per
  tree (a dead holder's lock reclaimed, a live one refused), and void if `HEAD` or the working tree moved while they
  ran; `QUALITY_FIRST` and `QUALITY_LAST` run at every tier's start and end. `coverage-gate`, a step a project may list, requires llvm-cov's `TOTAL` at 100 % on every dimension. SciForge
  runs its own tiers through them; `make quality-selftest` provokes each verdict in a scratch repository.
- **The calendar release every project cuts the same way** (`make/release.mk`, `tools/release.sh`): `vYEAR.MONTH.PATCH`,
  from `main` with a clean tree and, by default, a green `CI` run on `HEAD` (the tag is what publishes); the project's
  prechecks (`RELEASE_PRECHECK`, e.g. benchmarks against the last release; `SKIP_PRECHECK=1`), its advisory checks
  (`RELEASE_ADVISORY`, run in a dry run too, warning without blocking) and its version bump
  (`RELEASE_BUMP`, which may change only `RELEASE_FILES`); `CHANGELOG.md`'s non-empty `## Unreleased` filed under the
  version and annotating the tag; `DRY_RUN=1` rehearses all of it and restores the tree, as does any refusal before
  the commit; `RELEASE_NOTES` names another notes file, a directory in `RELEASE_FILES` (ending in `/`) lets the bump
  write files there, `RELEASE_EXTRA_TAGS` cuts tags beside `v<version>` (a bound module's) and `RELEASE_TAG_BODY` prints their
  annotation. `make release-selftest`
  cuts releases into a bare remote and provokes each refusal. SciForge cuts its own
  releases with it: the version of `sciforge-build` is now written by the release commit, and the tag is annotated.

### Contract changes
- None.

## 2026.10.3

### Fixed
- **The MISRA base self-test gates on clang-tidy's exit status**, as every consumer's `make misra` does: dropping
  `WarningsAsErrors` from `lint/clang-tidy-misra` stayed green before.
- **A crashing test keeps the failures printed before it and names itself.** With stdout redirected, a crash took
  every FAIL line with the unflushed buffer; a SIGSEGV, SIGABRT, SIGFPE or SIGILL now prints the running test and
  its last check, then goes to the handler it had before (a sanitizer's report follows). An exception escaping a
  test is reported at the last check that ran.
- **Binding:** an unsigned default above `LLONG_MAX` (`SIZE_MAX`) kept its value instead of reading as -1; a C++
  exception during module registration, or a failed allocation finishing a class, fails the import instead of
  ending the process.
- **Corpus:** an exception on the engine's side is a bug, never a pass; `out_of_contract` needs the engine to agree
  with the oracle or the stored row; a manifest's `api` must name an entry point the driver calls.
- **Release:** `make release` tags only a commit whose CI and CI selftest are green; `cibuildwheel` and `build`
  are pinned (inputs of `build-wheels.yml`, defaulting to 4.3.1 and 1.6.1); the Uncrustify tarball's sha256 is
  checked.

### Added
- `sciforge::bench::json_integer`, a count or a size written exactly (`json_number`'s `%.6g` writes 1048576 and
  1048577 alike).

### Contract changes
- None.

## 2026.10.2

### Contract changes
- **A `PyObject*` returned through `def<>` or `def_prop_ro` is a new reference** (2b616db): the caster took one
  of its own before. A binding that returned a borrowed reference must now return an owned one. Published sdists
  whose floor admits 2026.10.2 build against this meaning.
