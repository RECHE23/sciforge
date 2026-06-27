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
    T* held = reinterpret_cast<wrapper<T>*>(obj)->held;
    if (held == nullptr) {                                   // __new__ without __init__
      throw cast_error(std::string("uninitialized ") + class_name<T>());
    }
    return *held;
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
  // A getter returning a plain C++ value goes through its caster; a getter returning PyObject*
  // (a computed dict/tuple no value-caster can build, e.g. a shape tuple or an array-interface
  // dict) already owns a new reference and is returned directly — routing it through
  // caster<PyObject*> would Py_NewRef and leak.
  template <class T, PyObject* (*Getter)(), auto Get>
  PyObject* property_getter(PyObject* self,
                            void* /*closure*/)
  {
    try {
      T& self_obj = class_unwrap<T>(self);
      using R     = std::decay_t<decltype(Get(self_obj))>;
      if constexpr (std::is_same_v<R, PyObject*>) {
        return Get(self_obj); // contract: a PyObject*-returning getter returns a NEW ref (the
                              // getset protocol), passed straight through — never a borrowed one
      }
      else {
        return caster<R>::to_python(Get(self_obj));
      }
    } catch (const cast_error& err) {
      PyErr_SetString(PyExc_TypeError, err.what());
      return nullptr;
    } catch (...) {
      return set_cpp_error(Getter());
    }
  }

  namespace detail {
    // Call Factory(arg_0, ..., arg_n) from the parsed objects — the factory's parameters
    // map one-to-one to the constructor arguments (no leading self, unlike a method).
    template <auto Factory, std::size_t... I>
    auto invoke_factory(PyObject* const*          objs,
                        std::index_sequence<I...> /*seq*/)
    {
      using traits = function_traits<decltype(Factory)>;
      return Factory(caster<typename traits::template arg<I>>::from_python(objs[I])...);
    }
  }  // namespace detail

  // tp_init for a constructible wrapped type: parse the keyword arguments (the N3a arg-syntax
  // kw_spec), call the factory, and store its result as the held T — deleting any previous one
  // so __init__ stays re-callable.
  template <class T, PyObject* (*Getter)(), auto Factory>
  int class_init(PyObject* self,
                 PyObject* args,
                 PyObject* kwds)
  {
    using traits             = function_traits<decltype(Factory)>;
    constexpr std::size_t kN                     = traits::arity;
    kw_spec&              spec                   = kw_spec_for<Factory>();
    PyObject*             objs[kN == 0 ? 1 : kN] = {};
    if (detail::parse_kw(args, kwds, spec.format.c_str(), spec.kwlist.data(), objs,
                         std::make_index_sequence<kN> {}) == 0) {
      return -1;
    }
    for (std::size_t i = 0; i < kN; ++i) {
      if (objs[i] == nullptr) {
        objs[i] = spec.defaults[i];
      }
    }
    try {
      T           value = detail::invoke_factory<Factory>(objs, std::make_index_sequence<kN> {});
      // Build the new held BEFORE deleting the old one (strong guarantee): if the allocation
      // throws on re-__init__, the existing held stays intact rather than dangling. At first
      // __init__ held is null, so the delete is a no-op.
      T*          fresh = new T(std::move(value));
      wrapper<T>* w     = reinterpret_cast<wrapper<T>*>(self);
      delete w->held;
      w->held = fresh;
      return 0;
    } catch (const cast_error& err) {
      PyErr_SetString(PyExc_TypeError, err.what());
      return -1;
    } catch (...) {
      set_cpp_error(Getter());
      return -1;
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

    // Make the type constructible from Python: Type(args...) parses through the N3a arg-syntax
    // (positional + keyword/optional), calls Factory (a free function returning T), and stores
    // the result. Without this, the type is factory-only (DISALLOW_INSTANTIATION).
    template <auto Factory, class Spec0, class ... Specs>
    class_& def_init(Spec0    spec0,
                     Specs... specs)
    {
      using traits = function_traits<decltype(Factory)>;
      static_assert(std::is_same_v<typename traits::return_type, T>,
                    "def_init's factory must return the wrapped type T");
      static_assert(1 + sizeof...(Specs) == traits::arity,
                    "the number of arg() specifications must match the factory's parameter count");
      kw_spec& spec = kw_spec_for<Factory>();
      spec          = kw_spec {};
      add_arg(spec, spec0);
      (add_arg(spec, specs), ...);
      spec.kwlist.push_back(nullptr);
      init_ = class_init<T, Getter, Factory>;
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
      // Up to six slots: dealloc/methods/getset, optionally tp_new+tp_init when constructible,
      // then the terminator.
      PyType_Slot  slots[6] = {};
      std::size_t  n        = 0;
      unsigned int flags    = Py_TPFLAGS_DEFAULT;
      slots[n++] = {Py_tp_dealloc, reinterpret_cast<void*>(class_dealloc<T>)};
      slots[n++] = {Py_tp_methods, static_cast<void*>(class_methods<T>().data())};
      slots[n++] = {Py_tp_getset, static_cast<void*>(class_getsets<T>().data())};
      if (init_ != nullptr) {
        slots[n++] = {Py_tp_new, reinterpret_cast<void*>(PyType_GenericNew)};
        slots[n++] = {Py_tp_init, reinterpret_cast<void*>(init_)};
      }
      else {
        flags |= Py_TPFLAGS_DISALLOW_INSTANTIATION; // factory-only: no Python construction
      }
      slots[n] = {0, nullptr};
      PyType_Spec spec {class_name<T>(), static_cast<int>(sizeof(wrapper<T>)), 0, flags, slots};
      PyObject*   type = PyType_FromSpec(&spec);
      if (type == nullptr) {
        return; // a Python error is set; PyInit propagates it via the module attribute add
      }
      class_type<T>()  = type;
      const char* dot  = std::strrchr(class_name<T>(), '.');
      const char* attr = (dot != nullptr) ? dot + 1 : class_name<T>();
      PyModule_AddObjectRef(module_, attr, type);
    }

    PyObject* module_   = nullptr;
    initproc  init_     = nullptr;        // set by def_init; null = factory-only
    bool      finished_ = false;
  };
}  // namespace sciforge::binding

// Bridge a wrapped type T <-> its heap type as a per-argument caster: from_python unwraps a
// borrowed reference (valid for the call), to_python wraps a new copy. Expands to a
// namespace-scoped specialization (a standard explicit specialization must be defined inside
// its template's namespace, not via a global-qualified name — g++ rejects the latter). Write
// it once, at namespace scope, next to the class_<T> registration; no trailing semicolon.
#define SCIFORGE_WRAPPED(T)                                                  \
        namespace sciforge::binding {                                               \
          template <>                                                               \
          struct caster<T> {                                                        \
            static T&        from_python(PyObject * obj)                             \
            {                                                                       \
              return class_unwrap<T>(obj);                                          \
            }                                                                       \
            static PyObject* to_python(T value)                                     \
            {                                                                       \
              return class_wrap<T>(std::move(value));                              \
            }                                                                       \
          };                                                                        \
        }
