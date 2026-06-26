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
      if (PyTuple_Size(args) != static_cast<Py_ssize_t>(kN)) {
        throw cast_error("expected " + std::to_string(kN) + " argument(s), got " +
                         std::to_string(PyTuple_Size(args)));
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
}  // namespace sciforge::binding
