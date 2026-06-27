// SciForge binding substrate — declarative module surface (Limited-API, abi3-safe).
//
// SCIFORGE_MODULE(modname, "qualified.error", m) { m.def<&f>("f"); ... } expands to
// PyInit_<modname>: it creates the module, registers the error type once, binds the
// error getter once, runs the registration block (the m.def<…> / m.raw(…) calls
// accumulate a method table), and PyModule_AddFunctions. So the consumer writes
// m.def<&f>("f") instead of def<&getter, &f>("f") — the getter is baked into the
// builder. SCIFORGE_BINDING_ERROR_GETTER; forward-declares the getter so manual
// functions earlier in the file can call sciforge_module_error() too. One module per
// translation unit.
#pragma once

#include <Python.h>

#include <sciforge/binding/class.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>

#include <vector>

namespace sciforge::binding {

  // Accumulates a module's method table; the error getter is baked into its type, so
  // .def<&f>(...) forwards to def<Getter, &f>(...).
  template <PyObject* (*Getter)()>
  class module_builder {
  public:

    module_builder(std::vector<PyMethodDef>& table,
                   PyObject*                 module) : table_(table), module_(module)
    {}

    // Wrap a C++ type T as a heap type and add it to the module. The returned builder
    // accumulates the type's methods/properties and creates the type when it finalizes (at
    // the end of its full expression), so the usual one-statement chain just works.
    template <class T>
    class_<T, Getter> type(const char* qualified_name)
    {
      return class_<T, Getter> {qualified_name, module_};
    }

    template <auto Func>
    module_builder& def(const char* name,
                        const char* doc = nullptr)
    {
      table_.push_back(::sciforge::binding::def<Getter, Func>(name, doc));
      return *this;
    }

    template <auto Func, class Spec0, class... Specs>
    module_builder& def(const char* name,
                        const char* doc,
                        Spec0       spec0,
                        Specs...    specs)
    {
      table_.push_back(::sciforge::binding::def<Getter, Func>(name, doc, spec0, specs ...));
      return *this;
    }

    // A raw PyCFunction, for the handful of entries the dispatch does not cover
    // (variadic, owned-capsule factories, …).
    module_builder& raw(const char* name,
                        PyCFunction func,
                        int         flags,
                        const char* doc = nullptr)
    {
      table_.push_back(PyMethodDef {name, func, flags, doc});
      return *this;
    }

  private:

    std::vector<PyMethodDef>& table_;
    PyObject*                 module_;
  };
}  // namespace sciforge::binding

// Forward declaration of the module error getter (fixed name within the TU), so
// functions defined before SCIFORGE_MODULE can call sciforge_module_error().
#define SCIFORGE_BINDING_ERROR_GETTER PyObject* sciforge_module_error()

#define SCIFORGE_MODULE(modname, errorname, m)                                                  \
        namespace {                                                                                 \
          PyObject* sciforge_module_error_storage = nullptr;                                          \
        }                                                                                           \
        SCIFORGE_BINDING_ERROR_GETTER { return sciforge_module_error_storage; }                     \
        void sciforge_module_register(::sciforge::binding::module_builder<&sciforge_module_error>&);\
        namespace {                                                                                 \
          PyModuleDef sciforge_module_def = {PyModuleDef_HEAD_INIT, #modname, nullptr, -1,            \
                                             nullptr, nullptr, nullptr, nullptr, nullptr};            \
        }                                                                                           \
        PyMODINIT_FUNC PyInit_##modname()                                                           \
        {                                                                                           \
          PyObject* module = PyModule_Create(&sciforge_module_def);                              \
          if (module == nullptr) {                                                                \
            return nullptr;                                                                     \
          }                                                                                       \
          sciforge_module_error_storage = ::sciforge::binding::register_error(module, errorname); \
          if (sciforge_module_error_storage == nullptr) {                                         \
            Py_DECREF(module);                                                                  \
            return nullptr;                                                                     \
          }                                                                                       \
          static std::vector<PyMethodDef> sciforge_module_table;                                  \
          ::sciforge::binding::module_builder<&sciforge_module_error> m(sciforge_module_table,    \
                                                                        module);                  \
          sciforge_module_register(m);                                                            \
          sciforge_module_table.push_back(PyMethodDef {nullptr, nullptr, 0, nullptr});            \
          if (PyModule_AddFunctions(module, sciforge_module_table.data()) < 0) {                  \
            Py_DECREF(module);                                                                  \
            return nullptr;                                                                     \
          }                                                                                       \
          return module;                                                                          \
        }                                                                                           \
        void sciforge_module_register(::sciforge::binding::module_builder<&sciforge_module_error>& m)
