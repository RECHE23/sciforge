# sciforge-build

Build-time distribution of the SciForge C++ **binding substrate** headers
(`<sciforge/binding/...>`). It is **never a runtime dependency** — list it in your
`build-system.requires` and add `sciforge_build.get_include()` to your extension's
`include_dirs` at wheel-build time. Pure-Python (`py3-none-any`); CalVer, aligned with
the SciForge tag.

## Resolving the headers (canonical consumption pattern)

A wheel/sdist build (pip, `python -m build`, cibuildwheel) resolves the headers through
the `sciforge-build` package above. But a co-development checkout wants the **sibling
SciForge tree**, not a possibly stale pip-installed package. The canonical resolver in a
consumer's `setup.py` tries, in order, an explicit `SCIFORGE_INCLUDE` override, a sibling
`sciforge` checkout, then the package — so local dev wins, CI/wheel falls through to the
package:

```python
def _sciforge_include():
    header = os.path.join("sciforge", "binding", "error.hpp")
    env = os.environ.get("SCIFORGE_INCLUDE")
    if env and os.path.exists(os.path.join(env, header)):
        return env
    sibling = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sciforge", "include")
    if os.path.exists(os.path.join(sibling, header)):
        return sibling
    try:
        import sciforge_build
        return sciforge_build.get_include()
    except ImportError:
        raise SystemExit("sciforge headers not found: check out RECHE23/sciforge as a "
                         "sibling, pip install sciforge-build, or set SCIFORGE_INCLUDE")
```

Then `include_dirs=["include", _sciforge_include()]`, and have the Makefile `python`
target export `SCIFORGE_INCLUDE=$(SCIFORGE_INCLUDE)` (default `../sciforge/include`) so a
sibling build needs no installed `sciforge-build`. The `exists(error.hpp)` guard makes a
sibling that lacks `binding/` fall through to the package.
