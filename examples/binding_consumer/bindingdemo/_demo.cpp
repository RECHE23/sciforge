// Real abi3 C++ binding fixture for SciForge's binding-substrate self-test.
// N1: set_cpp_error (three branches), register_error, the GIL helpers.
// N2: per-argument casters + def<>-dispatch (plain C++ functions, no PyObject* in
// the body). Py_LIMITED_API is set on the compiler command line by setup.py.
#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/gil.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

  // The module's error type, created in module init via register_error.
  PyObject* g_error = nullptr;

  constexpr long long kThreshold = 4096; // consumer-owned policy; substrate owns the mechanism

  // A trivial deterministic workload so the GIL paths have something to run.
  long long work(long long n)
  {
    long long acc = 0;
    for (long long i = 0; i < n; ++i) {
      acc += i % 7;
    }
    return acc;
  }

  // --------------------------------------------------------------------------- N1
  PyObject* raise_runtime(PyObject* /*self*/,
                          PyObject* /*args*/)
  {
    try {
      throw std::runtime_error("boom from C++");
    } catch (...) {
      return sciforge::binding::set_cpp_error(g_error);
    }
  }

  PyObject* raise_bad_alloc(PyObject* /*self*/,
                            PyObject* /*args*/)
  {
    try {
      throw std::bad_alloc();
    } catch (...) {
      return sciforge::binding::set_cpp_error(g_error);
    }
  }

  PyObject* raise_unknown(PyObject* /*self*/,
                          PyObject* /*args*/)
  {
    try {
      throw 42;    // not a std::exception -> the "internal error" branch
    } catch (...) {
      return sciforge::binding::set_cpp_error(g_error);
    }
  }

  // Release the GIL only above the threshold; both paths must compute the same result.
  PyObject* compute(PyObject* /*self*/,
                    PyObject* args)
  {
    long long size = 0;
    if (PyArg_ParseTuple(args, "L", &size) == 0) {
      return nullptr;
    }
    long long result = 0;
    {
      sciforge::binding::scoped_gil_release release(size > kThreshold);
      result = work(size > 0 ? size : 0);
    }
    return PyLong_FromLongLong(result);
  }

  // Take a GIL release, then throw: the exception must surface AND the interpreter
  // must stay usable (the RAII restores the GIL during unwinding, before the catch).
  PyObject* release_then_throw(PyObject* /*self*/,
                               PyObject* /*args*/)
  {
    try {
      sciforge::binding::gil_release release;
      throw std::runtime_error("thrown while the GIL was released");
    } catch (...) {
      return sciforge::binding::set_cpp_error(g_error);
    }
  }

  // --------------------------------------------------------------------------- N2
  namespace sb = sciforge::binding;

  // The module's error-type getter, baked into each def<> as a template parameter.
  PyObject* demo_error()
  {
    return g_error;
  }

  // Plain C++ functions — one per caster + each dispatch path. No PyObject* visible.
  bool        is_even(long long n)
  {
    return n % 2 == 0;
  }                                                                           // int + bool

  double      scale(double    x,
                    long long k)
  {
    return x * static_cast<double>(k);
  }

  std::string echo(std::string s)
  {
    return s;
  }                                                                          // str

  std::string sv_echo(std::string_view s)
  {
    return std::string(s);
  }                                                                          // A10: str->view->str, one call

  Py_ssize_t  blen(sb::bytes_view b)
  {
    return b.size;
  }                                                                          // bytes-only

  std::vector<long long> upto(long long n)                                   // vector -> list
  {
    std::vector<long long> out;
    for (long long i = 0; i < (n > 0 ? n : 0); ++i) {
      out.push_back(i);
    }
    return out;
  }

  PyObject* identity(PyObject* o)
  {
    return o;
  }                                            // pass-through: borrowed in, new ref out (refcount test)

  void      noop()
  {
  }                                            // void -> None

  long long boom()
  {
    throw std::runtime_error("boom via dispatch");
  }                                                                           // throw -> module error

  // A capsule handle + its caster specialization (the per-binding pattern).
  struct handle {
    int value;
  };

  void handle_destructor(PyObject* capsule)
  {
    delete static_cast<handle*>(PyCapsule_GetPointer(capsule, "bindingdemo.handle"));
  }

  int handle_value(handle* h)
  {
    return h->value;
  }

  // make_handle returns an OWNED capsule, so it is registered manually rather than
  // through the dispatch (whose PyObject* return caster assumes a borrowed ref).
  PyObject* make_handle(PyObject* /*self*/,
                        PyObject* args)
  {
    long long value = 0;
    if (PyArg_ParseTuple(args, "L", &value) == 0) {
      return nullptr;
    }
    auto* h = new handle{static_cast<int>(value)};
    return PyCapsule_New(h, "bindingdemo.handle", handle_destructor);
  }
}  // namespace

// The handle caster lives in the substrate's namespace (the documented per-binding
// pattern); it references the fixture's local handle type within this TU.
namespace sciforge::binding {
  template <>
  struct caster<handle*> {
    static handle* from_python(PyObject* obj)
    {
      auto* h = static_cast<handle*>(PyCapsule_GetPointer(obj, "bindingdemo.handle"));
      if (h == nullptr) {
        PyErr_Clear();
        throw cast_error("expected a bindingdemo.handle capsule");
      }
      return h;
    }
  };
}  // namespace sciforge::binding

namespace {

  PyMethodDef methods[] = {
    // N1 — manual PyCFunctions.
    {"raise_runtime",      raise_runtime,      METH_NOARGS,  "Throw std::runtime_error -> module error."},
    {"raise_bad_alloc",    raise_bad_alloc,    METH_NOARGS,  "Throw std::bad_alloc -> MemoryError."},
    {"raise_unknown",      raise_unknown,      METH_NOARGS,  "Throw a non-exception -> internal error."},
    {"compute",            compute,            METH_VARARGS, "Sum i%%7 for i in range(size); releases the GIL above the threshold."},
    {"release_then_throw", release_then_throw, METH_NOARGS,  "Release the GIL then throw; the RAII must restore it."},
    {"make_handle",        make_handle,        METH_VARARGS, "Build a bindingdemo.handle capsule from an int."},
    // N2 — plain C++ functions through def<getter, func>.
    sb::def<&demo_error, &is_even>("is_even", "n -> n is even"),
    sb::def<&demo_error, &scale>("scale", "x, k -> x*k"),
    sb::def<&demo_error, &echo>("echo", "s -> s"),
    sb::def<&demo_error, &sv_echo>("sv_echo", "s -> s (via string_view)"),
    sb::def<&demo_error, &blen>("blen", "b -> len(b)"),
    sb::def<&demo_error, &upto>("upto", "n -> [0, n)"),
    sb::def<&demo_error, &identity>("identity", "o -> o (pass-through)"),
    sb::def<&demo_error, &noop>("noop", "-> None"),
    sb::def<&demo_error, &boom>("boom", "throw -> module error"),
    sb::def<&demo_error, &handle_value>("handle_value", "capsule -> value"),
    {nullptr, nullptr, 0, nullptr},
  };
  PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_demo", "SciForge binding-substrate self-test fixture.",
    -1, methods, nullptr, nullptr, nullptr, nullptr,
  };
}  // namespace

PyMODINIT_FUNC PyInit__demo(void)
{
  PyObject* m = PyModule_Create(&module);
  if (m == nullptr) {
    return nullptr;
  }
  g_error = sciforge::binding::register_error(m, "bindingdemo.error");
  if (g_error == nullptr) {
    Py_DECREF(m);
    return nullptr;
  }
  return m;
}
