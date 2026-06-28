// SciForge binding substrate — call dispatch (Limited-API, abi3-safe).
//
// def<Getter, Func>("name") yields a PyMethodDef whose PyCFunction extracts each
// positional argument through caster<Arg_i>, calls the plain C++ Func, converts the
// return through caster<Ret>, and wraps the whole thing in the N1 error bridge — so
// a binding writes plain C++ functions instead of the repeated parse -> call ->
// convert -> catch block. Fixed arity, positional only (optional/keyword -> N3a).
//
// The module's error type is supplied by a consumer getter baked in as a non-type
// template parameter (Getter), so the substrate holds no global and no module state.
// Uses only Py_LIMITED_API functions.
#pragma once

#include <Python.h>

#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/error.hpp>

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace sciforge::binding {

  // Signature deduction — free function pointers only. Member pointers are N3b.
  template <class F>
  struct function_traits;

  template <class R, class ... Args>
  struct function_traits<R (*)(Args...)> {
    using return_type                   = R;
    static constexpr std::size_t arity  = sizeof...(Args);
    template <std::size_t I>
    using arg = std::decay_t<std::tuple_element_t<I, std::tuple<Args...>>>;
  };

  namespace detail {

    // Extract every argument and call. The from_python pack expands as the call's
    // arguments, whose evaluation order is unspecified in C++ — safe here because the
    // casters are pure (convert-or-throw, no cross-argument side effects).
    template <auto Func, std::size_t... I>
    PyObject* invoke(PyObject* args,
                     std::index_sequence<I...> /*seq*/)
    {
      using traits = function_traits<decltype(Func)>;
      if constexpr (std::is_void_v<typename traits::return_type>) {
        Func(caster<typename traits::template arg<I>>::from_python(PyTuple_GetItem(args, static_cast<Py_ssize_t>(I)))...);
        Py_RETURN_NONE;
      }
      else {
        return caster<std::decay_t<typename traits::return_type>>::to_python(
          Func(caster<typename traits::template arg<I>>::from_python(PyTuple_GetItem(args, static_cast<Py_ssize_t>(I)))...));
      }
    }
  } // namespace detail

  // The generated PyCFunction. Getter() yields the module error type for the non-cast
  // branch (a cast_error becomes a Python TypeError instead).
  template <PyObject* (*Getter)(), auto Func>
  PyObject* call_wrapper(PyObject* /*self*/,
                         PyObject* args)
  {
    using traits             = function_traits<decltype(Func)>;
    constexpr std::size_t kN = traits::arity;
    try {
      const Py_ssize_t given = PyTuple_Size(args);
      if (given != static_cast<Py_ssize_t>(kN)) {
        throw cast_error("expected " + std::to_string(kN) + " argument(s), got " +
                         std::to_string(given));
      }
      return detail::invoke<Func>(args, std::make_index_sequence<kN>{});
    } catch (const cast_error& err) {
      PyErr_SetString(PyExc_TypeError, err.what());
      return nullptr;
    } catch (...) {
      return set_cpp_error(Getter());
    }
  }

  // Build the method-table entry. Name (and doc) are runtime; Getter + Func are baked
  // in as template parameters.
  template <PyObject* (*Getter)(), auto Func>
  PyMethodDef def(const char* name,
                  const char* doc = nullptr)
  {
    return PyMethodDef{name, call_wrapper<Getter, Func>, METH_VARARGS, doc};
  }

  // --- Keyword / optional arguments (METH_VARARGS | METH_KEYWORDS) ----------------
  //
  // arg("x") is a required parameter; arg("x") = value an optional one with a default.
  // Defaults are resolved to a Python object ONCE, at registration: a scalar/str
  // literal via PyLong/PyFloat/PyUnicode, and binding::none to Py_None (the call-time
  // sentinel for an absent PyObject* argument). At call time an absent optional uses
  // that stored default, so every parameter goes through caster<Arg_i>::from_python
  // uniformly. Required parameters must precede optional ones (PyArg's '|' rule).

  struct none_t {};
  inline constexpr none_t none {};

  template <class T>
  struct defaulted_arg {
    const char* name;
    T           value;
  };
  struct arg {
    const char* name;
    constexpr explicit arg(const char* n) : name(n)
    {}
    template <class T>
    constexpr defaulted_arg<T> operator=(T v) const
    {
      return defaulted_arg<T> {name, v};
    }
  };

  // Resolve a default to an OWNED Python reference (every branch, including None). On an
  // allocation failure the conversion returns nullptr; we set MemoryError and propagate the
  // nullptr — never throw, since this runs inside the extern "C" PyInit where a C++ exception
  // crossing the boundary is undefined behaviour. Uniform ownership is what lets def() safely
  // Py_XDECREF the stored defaults on a re-import (a borrowed None there would over-decref).
  template <class T>
  inline PyObject* default_object(T value)
  {
    if constexpr (std::is_same_v<T, none_t>) {
      return Py_NewRef(Py_None); // owned, not the borrowed singleton
    }
    else {
      PyObject* obj = nullptr;
      if constexpr (std::is_same_v<T, const char*>) {
        obj = PyUnicode_FromString(value);
      }
      else if constexpr (std::is_floating_point_v<T>) {
        obj = PyFloat_FromDouble(static_cast<double>(value));
      }
      else {
        static_assert(std::is_integral_v<T>, "unsupported default-argument type");
        obj = PyLong_FromLongLong(static_cast<long long>(value));
      }
      return obj != nullptr ? obj : PyErr_NoMemory(); // PyErr_NoMemory sets MemoryError, returns nullptr
    }
  }

  // Per-function keyword spec: kwlist (names + NULL), the all-'O' format, and the
  // resolved defaults (nullptr for a required parameter). One static per Func.
  struct kw_spec {
    std::vector<char*>     kwlist;
    std::string            format;
    std::vector<PyObject*> defaults;
    bool                   optional_started = false;
  };

  template <auto Func>
  inline kw_spec& kw_spec_for()
  {
    static kw_spec spec;
    return spec;
  }

  inline void add_arg(kw_spec&   spec,
                      const arg& a)
  {
    spec.kwlist.push_back(const_cast<char*>(a.name)); // PyArg's char** kwlist is read-only
    spec.format += 'O';
    spec.defaults.push_back(nullptr);
  }

  template <class T>
  inline void add_arg(kw_spec&                spec,
                      const defaulted_arg<T>& a)
  {
    if (!spec.optional_started) {
      spec.format          += '|';
      spec.optional_started = true;
    }
    spec.kwlist.push_back(const_cast<char*>(a.name));
    spec.format += 'O';
    spec.defaults.push_back(default_object(a.value));
  }

  namespace detail {
    template <std::size_t... I>
    int parse_kw(PyObject  * args,
                 PyObject  * kwargs,
                 const char* format,
                 char     ** kwlist,
                 PyObject ** objs,
                 std::index_sequence<I...> /*seq*/)
    {
      return PyArg_ParseTupleAndKeywords(args, kwargs, format, kwlist, &objs[I] ...);
    }

    template <auto Func, std::size_t... I>
    PyObject* invoke_objs(PyObject* const* objs,
                          std::index_sequence<I...> /*seq*/)
    {
      using traits = function_traits<decltype(Func)>;
      if constexpr (std::is_void_v<typename traits::return_type>) {
        Func(caster<typename traits::template arg<I>>::from_python(objs[I])...);
        Py_RETURN_NONE;
      }
      else {
        return caster<std::decay_t<typename traits::return_type>>::to_python(
          Func(caster<typename traits::template arg<I>>::from_python(objs[I])...));
      }
    }
  }  // namespace detail

  template <PyObject* (*Getter)(), auto Func>
  PyObject* call_wrapper_kw(PyObject* /*self*/,
                            PyObject* args,
                            PyObject* kwargs)
  {
    using traits             = function_traits<decltype(Func)>;
    constexpr std::size_t kN                     = traits::arity;
    kw_spec&              spec                   = kw_spec_for<Func>(); // non-const: PyArg's kwlist is char**
    PyObject*             objs[kN == 0 ? 1 : kN] = {};
    if (detail::parse_kw(args, kwargs, spec.format.c_str(), spec.kwlist.data(), objs,
                         std::make_index_sequence<kN> {}) == 0) {
      return nullptr; // PyArg sets TypeError: bad type / missing required / unknown keyword
    }
    for (std::size_t i = 0; i < kN; ++i) {
      if (objs[i] == nullptr) {
        objs[i] = spec.defaults[i]; // an absent optional uses its stored owned default
      }
      if (objs[i] == nullptr) {
        return PyErr_NoMemory();    // a default that failed to allocate at registration (OOM)
      }
    }
    try {
      return detail::invoke_objs<Func>(objs, std::make_index_sequence<kN> {});
    } catch (const cast_error& err) {
      PyErr_SetString(PyExc_TypeError, err.what());
      return nullptr;
    } catch (...) {
      return set_cpp_error(Getter());
    }
  }

  // Keyword overload of def: requires at least one arg() spec (so it never competes
  // with the positional def above). Populates this Func's kw_spec once, here.
  template <PyObject* (*Getter)(), auto Func, class Spec0, class ... Specs>
  PyMethodDef def(const char* name,
                  const char* doc,
                  Spec0       spec0,
                  Specs...    specs)
  {
    // One arg() spec per parameter: otherwise call_wrapper_kw reads spec.defaults out
    // of bounds and the all-'O' format disagrees with parse_kw's pointer count (UB).
    static_assert(1 + sizeof...(Specs) == function_traits<decltype(Func)>::arity,
                  "the number of arg() specifications must match the function's parameter count");
    kw_spec& spec = kw_spec_for<Func>();
    for (PyObject* owned : spec.defaults) {
      Py_XDECREF(owned); // release the previous defaults (a re-import re-registers); nullptr is a no-op
    }
    spec = kw_spec {};
    add_arg(spec, spec0);
    (add_arg(spec, specs), ...);
    spec.kwlist.push_back(nullptr); // NULL-terminate the kwlist
    // The two-step cast (-> void(*)() -> PyCFunction) is the standard CPython idiom for storing
    // a METH_KEYWORDS function (signature self,args,kwargs) in a PyCFunction slot: a direct cast
    // between unrelated function-pointer types is ill-formed, so we route through void(*)() (as
    // CPython's own PyCFunction_NewEx and pybind11 do; ~14 sites across the ecosystem).
    return PyMethodDef {name,
                        reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(call_wrapper_kw<Getter, Func>)),
                        METH_VARARGS | METH_KEYWORDS, doc};
  }
}  // namespace sciforge::binding
