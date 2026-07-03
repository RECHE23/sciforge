# The binding helpers

`<sciforge/binding/…>` is the shared substrate for the abi3 CPython extensions (REAL, SciLex). It
targets the **Limited API** (`Py_LIMITED_API`), so one wheel spans CPython versions, and it keeps the
per-project extension code to the module's own logic.

## What each header provides

- **`module.hpp`** — `SCIFORGE_MODULE(name, error_qualname, m){ … }` declares the module and its
  exception type; `m.def<&fn>("name", doc)` binds a typed C++ function, `m.raw("name", cfn, flags, doc)`
  binds a hand-written `PyCFunction`.
- **`convert.hpp`** — `caster<T>` converts between Python objects and C++ values for the `def<>` path
  (scalars, strings, sequences), so a bound function reads native types.
- **`dispatch.hpp`** — the `def<>` machinery that adapts a C++ signature to a `PyCFunction`.
- **`error.hpp`** — `SCIFORGE_BINDING_ERROR_GETTER;` plus `set_cpp_error(module_error)`: bridge an
  in-flight C++ exception to a Python error (`bad_alloc → MemoryError`, other `std::exception →` the
  module error), so no C++ exception crosses the C-API boundary.
- **`gil.hpp`** — `gil_release` (RAII): drop the GIL around a pure-C++ region and restore it on every
  exit, including a throw.

## Shape of a binding

```cpp
#include <sciforge/binding/module.hpp>
// … other binding/ headers as needed

SCIFORGE_BINDING_ERROR_GETTER;

SCIFORGE_MODULE(_yourmod, "yourmod.error", m) {
    m.def<&your_function>("your_function", "docstring");
}
```

The GIL-release threshold and the exact error surface are the consumer's to choose; the substrate only
provides the mechanism.
