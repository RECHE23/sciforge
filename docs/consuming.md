# Consuming SciForge

SciForge is a header-only C++ backplane plus a small pure-Python build helper. There is no library to
link; a consumer picks up the headers one of three ways.

## 1. Sibling checkout (the co-development default)

Check SciForge out next to the consumer and point at its `include/`:

```
projects/
  sciforge/     ← here
  real-regex/   ← consumer
  scilex/
```

The consumers' Makefiles default `SCIFORGE_INCLUDE ?= ../sciforge/include` (and `SCIFORGE_LINT`,
`SCIFORGE_TOOLS`, `SCIFORGE_PYTHON` alongside). Nothing else is needed for a local build.

## 2. CMake `FetchContent` (a pinned tag)

Fetch a CalVer tag and use its include dir:

```cmake
FetchContent_Declare(sciforge GIT_REPOSITORY https://github.com/RECHE23/sciforge GIT_TAG v2026.7.0)
FetchContent_MakeAvailable(sciforge)
target_include_directories(your_target PRIVATE ${sciforge_SOURCE_DIR}/include)
```

Pin the same tag your CI's reusable-workflow references use (see `CONVENTIONS.md`).

## 3. The `sciforge-build` Python package

The build backend used by the bindings' `pyproject.toml` (headers for the abi3 extension, the build
glue). Depend on it with a **floor, never an exact pin**:

```toml
requires = ["sciforge-build>=2026.7.0"]
```

A floor picks up a compatible fix without a lockstep release; the backend stays backward-compatible
within a CalVer line.
