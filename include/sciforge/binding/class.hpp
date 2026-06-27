// SciForge binding substrate — class_<T>: wrap a C++ type as an abi3 heap type.
//
// class_<T, ErrorGetter>("pkg.Name") creates a CPython heap type whose instances hold a
// heap-allocated T at a compile-time-fixed offset, and accumulates its methods/properties:
//   .def<&free_fn>("m")          — a method: free_fn's FIRST parameter is the unwrapped
//                                  instance (T& / const T&), the rest are dispatched like a
//                                  module function (positional + the N3a arg-syntax). There
//                                  is NO member-pointer machinery: a real C::method is wrapped
//                                  in a free function at the call site.
//   .def_prop_ro<&getter>("p")   — a computed read-only property (getter(const T&) -> R).
//   .raw("m", fn, flags)         — a manual PyCFunction, for the non-castable cases.
//
// Storage (the make-or-break under Py_LIMITED_API 3.10): an instance is
// wrapper<T> { PyObject_HEAD; T* held; }. The T lives on the heap; the pointer sits right
// after the object header, so it is reached by reinterpret_cast at a compile-time offset —
// PyObject_GetTypeData (3.12+) is never used. This is exactly what the hand-rolled types do.
//
// caster<T> (from SCIFORGE_WRAPPED(T)) bridges T <-> the heap type: from_python unwraps a
// borrowed reference, to_python wraps a copy. So a module function can take or return a T.
#pragma once

#include <Python.h>

#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>

#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sciforge::binding {

  // The instance layout: the object header followed by a heap pointer to the C++ value.
  template <class T>
  struct wrapper {
    PyObject_HEAD
    T* held;
  };

  // Per-T registry: the created heap type, its qualified name, and the method/getset tables
  // (which must outlive PyType_FromSpec). One set of statics per wrapped type.
  template <class T>
  inline PyObject*& class_type()
  {
    static PyObject* type = nullptr;
    return type;
  }

  template <class T>
  inline const char*& class_name()
  {
    static const char* name = "?";
    return name;
  }

  template <class T>
  inline std::vector<PyMethodDef>& class_methods()
  {
    static std::vector<PyMethodDef> table;
    return table;
  }

  template <class T>
  inline std::vector<PyGetSetDef>& class_getsets()
  {
    static std::vector<PyGetSetDef> table;
    return table;
  }

  // Unwrap a borrowed instance to a reference into its held T (valid for the call). A wrong
  // type becomes a cast_error -> TypeError.
  template <class T>
  inline T& class_unwrap(PyObject* obj)
  {
    PyObject* type = class_type<T>();
    if (type == nullptr || PyObject_IsInstance(obj, type) <= 0) {
      PyErr_Clear();
      throw cast_error(std::string("expected a ") + class_name<T>());
    }
    return *reinterpret_cast<wrapper<T>*>(obj)->held;
  }

  // Wrap a C++ value in a new instance (a fresh heap copy owned by the object).
  template <class T>
  inline PyObject* class_wrap(T value)
  {
    PyObject* type = class_type<T>();
    if (type == nullptr) {
      PyErr_SetString(PyExc_SystemError, "wrapped type not registered");
      return nullptr;
    }
    PyObject* obj = PyType_GenericAlloc(reinterpret_cast<PyTypeObject*>(type), 0);
    if (obj == nullptr) {
      return nullptr;
    }
    T* held = new (std::nothrow) T(std::move(value));
    if (held == nullptr) {
      Py_DECREF(obj);
      return PyErr_NoMemory();
    }
    reinterpret_cast<wrapper<T>*>(obj)->held = held;
    return obj;
  }

  template <class T>
  inline void class_dealloc(PyObject* self)
  {
    delete reinterpret_cast<wrapper<T>*>(self)->held;
    PyTypeObject* tp = Py_TYPE(self);
    PyObject_Free(self);
    Py_DECREF(reinterpret_cast<PyObject*>(tp));
  }

  namespace detail {
    // Call free_fn(self, arg_1, ..., arg_n) where the args come from the positional tuple
    // (the function's first parameter is the instance, so the I-th tuple item feeds the
    // (I+1)-th function parameter).
    template <auto Func, class T, std::size_t... I>
    PyObject* invoke_method(T&        self,
                            PyObject* args,
                            std::index_sequence<I...> /*seq*/)
    {
      using traits = function_traits<decltype(Func)>;
      if constexpr (std::is_void_v<typename traits::return_type>) {
        Func(self, caster<typename traits::template arg<I + 1>>::from_python(
               PyTuple_GetItem(args, static_cast<Py_ssize_t>(I)))...);
        Py_RETURN_NONE;
      }
      else {
        return caster<std::decay_t<typename traits::return_type>>::to_python(
          Func(self, caster<typename traits::template arg<I + 1>>::from_python(
                 PyTuple_GetItem(args, static_cast<Py_ssize_t>(I)))...));
      }
    }
  }  // namespace detail

  // The generated PyCFunction for a method: unwrap self, then dispatch the remaining
  // positional arguments exactly like a free function.
  template <class T, PyObject* (*Getter)(), auto Func>
  PyObject* call_method(PyObject* self,
                        PyObject* args)
  {
    using traits                 = function_traits<decltype(Func)>;
    constexpr std::size_t kArity = traits::arity;
    static_assert(kArity >= 1, "a method's first parameter must be the instance");
    constexpr std::size_t kArgs = kArity - 1;
    try {
      if (PyTuple_Size(args) != static_cast<Py_ssize_t>(kArgs)) {
        throw cast_error("expected " + std::to_string(kArgs) + " argument(s), got " +
                         std::to_string(PyTuple_Size(args)));
      }
      T& self_obj = class_unwrap<T>(self);
      return detail::invoke_method<Func, T>(self_obj, args, std::make_index_sequence<kArgs> {});
    } catch (const cast_error& err) {
      PyErr_SetString(PyExc_TypeError, err.what());
      return nullptr;
    } catch (...) {
      return set_cpp_error(Getter());
    }
  }

  // The generated getter for a computed read-only property: unwrap self, call getter(self).
  template <class T, PyObject* (*Getter)(), auto Get>
  PyObject* property_getter(PyObject* self,
                            void* /*closure*/)
  {
    try {
      T& self_obj = class_unwrap<T>(self);
      return caster<std::decay_t<decltype(Get(self_obj))>>::to_python(Get(self_obj));
    } catch (const cast_error& err) {
      PyErr_SetString(PyExc_TypeError, err.what());
      return nullptr;
    } catch (...) {
      return set_cpp_error(Getter());
    }
  }

  // The builder. Methods/properties accumulate into the per-T tables; the heap type is
  // created (and added to the module) once, when the builder is finalized at the end of its
  // full expression — so the usual one-statement chain just works:
  //   m.type<Widget>("pkg.Widget").def<&area>("area").def_prop_ro<&width>("width");
  template <class T, PyObject* (*Getter)()>
  class class_ {
  public:

    class_(const char* qualified_name,
           PyObject*   module)
      : module_(module)
    {
      class_name<T>()    = qualified_name;
      class_methods<T>() = {};
      class_getsets<T>() = {};
    }

    class_(const class_&)            = delete;
    class_& operator=(const class_&) = delete;

    ~class_()
    {
      finish();
    }

    template <auto Func>
    class_& def(const char* name,
                const char* doc = nullptr)
    {
      class_methods<T>().push_back(PyMethodDef {name, call_method<T, Getter, Func>, METH_VARARGS, doc});
      return *this;
    }

    template <auto Get>
    class_& def_prop_ro(const char* name,
                        const char* doc = nullptr)
    {
      class_getsets<T>().push_back(
        PyGetSetDef {name, property_getter<T, Getter, Get>, nullptr, const_cast<char*>(doc), nullptr});
      return *this;
    }

    class_& raw(const char* name,
                PyCFunction func,
                int         flags,
                const char* doc = nullptr)
    {
      class_methods<T>().push_back(PyMethodDef {name, func, flags, doc});
      return *this;
    }

  private:

    void finish()
    {
      if (finished_ || class_type<T>() != nullptr) {
        return;
      }
      finished_ = true;
      class_methods<T>().push_back(PyMethodDef {nullptr, nullptr, 0, nullptr});
      class_getsets<T>().push_back(PyGetSetDef {nullptr, nullptr, nullptr, nullptr, nullptr});
      PyType_Slot slots[] = {
        {Py_tp_dealloc, reinterpret_cast<void*>(class_dealloc<T>)},
        {Py_tp_methods, static_cast<void*>(class_methods<T>().data())},
        {Py_tp_getset, static_cast<void*>(class_getsets<T>().data())},
        {0, nullptr},
      };
      PyType_Spec spec {class_name<T>(), static_cast<int>(sizeof(wrapper<T>)), 0,
                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, slots};
      PyObject* type = PyType_FromSpec(&spec);
      if (type == nullptr) {
        return; // a Python error is set; PyInit propagates it via the module attribute add
      }
      class_type<T>()  = type;
      const char* dot  = std::strrchr(class_name<T>(), '.');
      const char* attr = (dot != nullptr) ? dot + 1 : class_name<T>();
      PyModule_AddObjectRef(module_, attr, type);
    }

    PyObject* module_   = nullptr;
    bool      finished_ = false;
  };
}  // namespace sciforge::binding

// Bridge a wrapped type T <-> its heap type as a per-argument caster: from_python unwraps a
// borrowed reference (valid for the call), to_python wraps a new copy. Write it once next to
// the class_<T> registration. T must be the exact type passed to class_<T>.
#define SCIFORGE_WRAPPED(T)                                                  \
        template <>                                                                 \
        struct ::sciforge::binding::caster<T> {                                     \
          static T&        from_python(PyObject * obj)                               \
          {                                                                         \
            return ::sciforge::binding::class_unwrap<T>(obj);                       \
          }                                                                         \
          static PyObject* to_python(T value)                                       \
          {                                                                         \
            return ::sciforge::binding::class_wrap<T>(std::move(value));            \
          }                                                                         \
        }
