// SciForge binding substrate — per-argument casters (Limited-API, abi3-safe).
//
// caster<T> bridges one C++ value <-> one Python object. Contract:
//   from_python(PyObject*) -> a C++ value or a BORROWED view (never an owning
//                             PyObject*); throws binding::cast_error on a type
//                             mismatch ("expected X, got Y").
//   to_python(const T&)    -> a NEW reference.
// Only the site-justified types are specialized (anti-cruft): bool, the integral
// types, double, str (std::string / std::string_view, str-only), bytes_view
// (bytes-only), std::vector<T> (return side), and PyObject* (pass-through). A
// binding adds its own capsule handles by specializing caster<HandleT> (see the
// pattern note at the bottom). Uses only Py_LIMITED_API functions.
#pragma once

#include <Python.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sciforge::binding {

  // A conversion failure (wrong Python type / out of range). The dispatch maps it to
  // a Python TypeError; it never reaches set_cpp_error.
  struct cast_error : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  // The Python type name of an object, for "expected X, got Y" messages. Limited-API
  // (no tp_name access): go through type(o).__name__. Only called on the error path.
  inline std::string py_type_name(PyObject* obj)
  {
    PyObject*   type = PyObject_Type(obj);                                   // new ref
    PyObject*   name = (type != nullptr) ? PyObject_GetAttrString(type, "__name__") : nullptr;
    std::string out  = "?";
    if (name != nullptr) {
      Py_ssize_t  size = 0;
      const char* data = PyUnicode_AsUTF8AndSize(name, &size);
      if (data != nullptr) {
        out.assign(data, static_cast<std::size_t>(size));
      }
    }
    Py_XDECREF(name);
    Py_XDECREF(type);
    return out;
  }

  // Primary template — undefined; only the specializations below exist.
  template <class T, class Enable = void>
  struct caster;

  // PyObject* — pass-through. Borrowed in (the dispatch hands the borrowed tuple item
  // straight through); new ref out (the caller is handing ownership to Python).
  template <>
  struct caster<PyObject*> {
    static PyObject* from_python(PyObject* obj)
    {
      return obj;
    }                                                                        // borrowed

    static PyObject* to_python(PyObject* obj)
    {
      return Py_NewRef(obj);
    }                                                                        // new ref
  };

  // bool — truthiness in (matches the 'p' format), True/False out.
  template <>
  struct caster<bool> {
    static bool from_python(PyObject* obj)
    {
      const int truth = PyObject_IsTrue(obj);
      if (truth < 0) {
        throw cast_error("expected a truth value");
      }
      return truth != 0;
    }

    static PyObject* to_python(bool value)
    {
      return PyBool_FromLong(value ? 1 : 0);
    }
  };

  // Integers — ONE caster for every integral type (int, long, long long, Py_ssize_t).
  // Separate caster<long long> + caster<Py_ssize_t> would be the SAME specialization
  // on LLP64 (Windows, where Py_ssize_t is long long) and fail to compile, so the
  // integral types are handled uniformly through long long + a range check.
  template <class T>
  struct caster<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static T from_python(PyObject* obj)
    {
      if (PyLong_Check(obj) == 0) {
        throw cast_error("expected int, got " + py_type_name(obj));
      }
      const long long wide = PyLong_AsLongLong(obj);
      if (wide == -1 && PyErr_Occurred() != nullptr) {
        PyErr_Clear();
        throw cast_error("int out of range");
      }
      if (wide < static_cast<long long>(std::numeric_limits<T>::min()) ||
          wide > static_cast<long long>(std::numeric_limits<T>::max())) {
        throw cast_error("int out of range");
      }
      return static_cast<T>(wide);
    }

    static PyObject* to_python(T value)
    {
      return PyLong_FromLongLong(static_cast<long long>(value));
    }
  };

  // double.
  template <>
  struct caster<double> {
    static double from_python(PyObject* obj)
    {
      const double value = PyFloat_AsDouble(obj);
      if (value == -1.0 && PyErr_Occurred() != nullptr) {
        PyErr_Clear();
        throw cast_error("expected float, got " + py_type_name(obj));
      }
      return value;
    }

    static PyObject* to_python(double value)
    {
      return PyFloat_FromDouble(value);
    }
  };

  // str (only) — shared UTF-8 extraction; the returned view is valid only while the
  // str object is alive (during the call).
  inline std::string_view as_utf8(PyObject* obj)
  {
    if (PyUnicode_Check(obj) == 0) {
      throw cast_error("expected str, got " + py_type_name(obj));
    }
    Py_ssize_t  size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(obj, &size);
    if (data == nullptr) {
      throw cast_error("invalid str");
    }
    return std::string_view(data, static_cast<std::size_t>(size));
  }

  template <>
  struct caster<std::string_view> {
    // A10: the view borrows the str's buffer — it must NOT escape the call.
    static std::string_view from_python(PyObject* obj)
    {
      return as_utf8(obj);
    }

    static PyObject*         to_python(std::string_view value)
    {
      return PyUnicode_FromStringAndSize(value.data(), static_cast<Py_ssize_t>(value.size()));
    }
  };

  template <>
  struct caster<std::string> {
    static std::string from_python(PyObject* obj)
    {
      return std::string(as_utf8(obj));
    }

    static PyObject*   to_python(const std::string& value)
    {
      return PyUnicode_FromStringAndSize(value.data(), static_cast<Py_ssize_t>(value.size()));
    }
  };

  // bytes (only). A borrowed view of the bytes buffer (valid during the call).
  struct bytes_view {
    const char* data = nullptr;
    Py_ssize_t  size = 0;
  };

  template <>
  struct caster<bytes_view> {
    static bytes_view from_python(PyObject* obj)
    {
      if (PyBytes_Check(obj) == 0) {
        throw cast_error("expected bytes, got " + py_type_name(obj));
      }
      char*      data = nullptr;
      Py_ssize_t size = 0;
      if (PyBytes_AsStringAndSize(obj, &data, &size) < 0) {
        throw cast_error("invalid bytes");
      }
      return bytes_view{data, size};
    }
  };

  // std::optional<T> (arg side) — None -> std::nullopt, otherwise caster<T>::from_python.
  // The site: an optional argument that is "a T or None" and must stay plain C++. It
  // composes with the arg-syntax None sentinel: an optional arg whose default is
  // binding::none is materialized once to Py_None, so an absent OR an explicit None
  // argument both arrive here as Py_None and become std::nullopt; any other value goes
  // through caster<T>. The motivating site is an optional where None is a genuine value
  // distinct from absent (e.g. scinum asarray's z=str-or-None). An optional that defaults
  // to a concrete value instead (None must be rejected) uses caster<T> + arg(...)=literal,
  // NOT this. to_python is intentionally absent: no return site produces an optional yet —
  // add it the day one does (anti-cruft).
  template <class T>
  struct caster<std::optional<T>, void> {
    static std::optional<T> from_python(PyObject* obj)
    {
      if (obj == Py_None) {
        return std::nullopt;
      }
      return caster<T>::from_python(obj);
    }
  };

  // std::vector<T> -> list (return side; built element-wise through caster<T>).
  template <class T>
  struct caster<std::vector<T>, void> {
    static PyObject* to_python(const std::vector<T>& items)
    {
      PyObject* list = PyList_New(static_cast<Py_ssize_t>(items.size()));
      if (list == nullptr) {
        throw cast_error("could not allocate list");
      }
      for (std::size_t i = 0; i < items.size(); ++i) {
        PyObject* element = caster<T>::to_python(items[i]);
        if (element == nullptr) {
          Py_DECREF(list);
          throw cast_error("could not convert list element");
        }
        PyList_SetItem(list, static_cast<Py_ssize_t>(i), element);     // steals element
      }
      return list;
    }
  };

  // Per-binding capsule handles: a binding specializes caster<HandleT> itself, e.g.
  //
  //   template <> struct sciforge::binding::caster<cursor_state*> {
  //       static cursor_state* from_python(PyObject* o) {
  //           auto* p = static_cast<cursor_state*>(PyCapsule_GetPointer(o, "name"));
  //           if (p == nullptr) throw cast_error("expected a <name> capsule");
  //           return p;
  //       }
  //   };
  //
  // This is documented, not shipped — the substrate carries no binding-specific types.
}  // namespace sciforge::binding
