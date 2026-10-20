# Adopting SciForge's gates, releases and pins

How a project takes up the verification tiers (`make/quality.mk`), the calendar release (`make/release.mk`) and the
pin check (`tools/check-pins.sh`). Each file's header documents its variables; this page puts them together in the
order a new project needs them. [Consuming SciForge](consuming.md) covers the test harness and the binding headers.

All three are read from a **sibling checkout** (`../sciforge`), which the project's CI pins to a tag of its own.

## 1. The verification tiers

Name the project's make targets that each tier runs, then include `quality.mk`:

```make
SCIFORGE_MAKE    ?= ../sciforge/make
QUALITY_COMMIT   := format-check coverage-gate
QUALITY_PUSH     := check-pins test test-gcc sanitize lint-gate doc
QUALITY_RELEASE  := bench-diff fuzz-long
-include $(SCIFORGE_MAKE)/quality.mk
```

- `make check-commit` runs `QUALITY_COMMIT`; `check-push` adds `QUALITY_PUSH`; `check-release` adds
  `QUALITY_RELEASE`. Steps run in order, fail fast, are timed, and a run is void if the tree changed meanwhile:
  leave the tree alone while a tier runs.
- A step that needs arguments is a target of its own (`test-gcc:` calling `test CXX=g++-14`), so that
  `make <step>` reproduces a failure alone.
- `coverage-gate` requires the `TOTAL` line of `QUALITY_COVERAGE`'s llvm-cov report at 100 % on every dimension.
- `-include` keeps every other target working in a checkout without the sibling (CI pins its own SciForge).

## 2. The calendar release

Name the files that carry the version and a target that writes it into them, then include `release.mk`:

```make
RELEASE_FILES      := pyproject.toml python/mypackage/__init__.py
RELEASE_CI_IGNORED := *.md docs/*   # ci.yml's paths-ignore
RELEASE_PRECHECK   := bench-diff    # must pass before the tag
-include $(SCIFORGE_MAKE)/release.mk

release-bump:
	@test -n "$(VERSION)" || { echo "release-bump: VERSION= is required"; exit 1; }
	@sed -i.bak -E "s/^version = \".*\"/version = \"$(VERSION)\"/" pyproject.toml && rm -f pyproject.toml.bak
```

- `CHANGELOG.md` keeps a `## Unreleased` section at its top. The release files its entries under the version and
  date, and annotates the tag with them; an empty section is refused.
- `make release DRY_RUN=1` runs every guard and the bump, prints the tag's annotation, and restores the tree.
- The release refuses a HEAD whose CI (`RELEASE_CI`, default `CI`) has not succeeded, unless HEAD changed only
  paths in `RELEASE_CI_IGNORED` since that run.
- The bump may change `RELEASE_FILES` and the notes alone: any other file changed refuses the release.
- A change goes out when it is ready: a fix, an addition or a speed-up is released once `check-release` passes. Only a
  change that can break existing code waits for the first release of a month (`YYYY.M.0`), announced under
  **Breaking** in `## Unreleased`.

## 3. The pins

A consumer names SciForge's version in its workflows (`uses: …@vX`, `sciforge-ref: vX`, a checkout's `ref:`) and in
CMake (`set(SCIFORGE_TAG "vX")`). `tools/check-pins.sh` fails when they name more than one version, when one names
none (`@main`, a commit), when a `sciforge-build>=` floor exceeds the pin, and when the sibling checkout the
Makefile reads is not exactly the pinned tag, unmodified:

```make
SCIFORGE_TOOLS ?= ../sciforge/tools
check-pins:
	@if test -x $(SCIFORGE_TOOLS)/check-pins.sh; then $(SCIFORGE_TOOLS)/check-pins.sh .; \
	 else echo "check-pins: WARN — $(SCIFORGE_TOOLS)/check-pins.sh absent, skipped"; fi
```

To work on SciForge and a consumer together, `SCIFORGE_UNPINNED_OK=1` turns the sibling check into a warning; the
consumer then pins the SciForge release that carries that work before it pushes.

## 4. Raising the pin

After a SciForge release: check out its tag in the sibling, change every pin of the consumer to it in one commit,
and run `make check-pins`. A step the new release adds to the tiers (a gate a workflow now requires, say) is the
consumer's to wire in the same commit.
