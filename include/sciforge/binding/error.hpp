// SciForge binding substrate — error bridge (Limited-API, abi3-safe).
//
// One place for the C++ -> Python exception bridge every ecosystem binding needs:
// no C++ exception is allowed to cross the C-API. Parameterized by the module's
// error type so each binding keeps its own exception class (e.g. real.error).
//
// Uses only Py_LIMITED_API functions (PyErr_NoMemory / PyErr_SetString /
// PyErr_NewException / PyModule_AddObjectRef), so it compiles into one abi3 wheel
// per platform (cp310+). The consumer defines Py_LIMITED_API before including this.
#pragma once

#include <Python.h>

#include <cstring>
#include <exception>
#include <new>

namespace sciforge::binding {

  // Translate the in-flight C++ exception into a Python error and return nullptr, so a
  // binding entry point can write
  //   try { ... } catch (...) { return set_cpp_error(module_error_type); }
  // The argument is the module's error TYPE (catch (...) binds no exception object — the
  // in-flight exception is re-examined here via a bare `throw;`). Branch order matters:
  // bad_alloc maps to MemoryError, every other std::exception to `error_type` with
  // .what(), and anything else to a generic "internal error".
  inline PyObject* set_cpp_error(PyObject* error_type)
  {
    try {
      throw;
    } catch (const std::bad_alloc&) {
      return PyErr_NoMemory();
    } catch (const std::exception& ex) {
      PyErr_SetString(error_type, ex.what());
      return nullptr;
    } catch (...) {
      PyErr_SetString(error_type, "internal error");
      return nullptr;
    }
  }

  // Create the module's exception type and attach it. `qualified_name` is the dotted
  // name for the exception (e.g. "real.error"); the module attribute is its trailing
  // component. `base` is optional — pass a base class (e.g. re.error) or nullptr to
  // derive from Exception. Returns a new reference to the error type (the caller keeps
  // it for set_cpp_error), or nullptr on failure with a Python error set.
  inline PyObject* register_error(PyObject  * module,
                                  const char* qualified_name,
                                  PyObject  * base = nullptr)
  {
    PyObject* err = PyErr_NewException(qualified_name, base, nullptr);
    if (err == nullptr) {
      return nullptr;
    }
    const char* dot  = std::strrchr(qualified_name, '.');
    const char* attr = (dot != nullptr) ? dot + 1 : qualified_name;
    if (PyModule_AddObjectRef(module, attr, err) < 0) {
      Py_DECREF(err);
      return nullptr;
    }
    return err;
  }
}  // namespace sciforge::binding
