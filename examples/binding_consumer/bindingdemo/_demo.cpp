// Real abi3 C++ binding fixture for SciForge's binding-substrate self-test.
// N1: set_cpp_error (three branches), register_error, the GIL helpers.
// N2: per-argument casters + def<>-dispatch (plain C++ functions).
// N3a: SCIFORGE_MODULE (the declarative module surface) + the keyword/optional
//      arg-syntax. Py_LIMITED_API is set on the compiler command line by setup.py.
#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/gil.hpp>
#include <sciforge/binding/module.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// The module error getter is defined by SCIFORGE_MODULE at the bottom; forward-declare
// it so the manual functions below can route through it.
SCIFORGE_BINDING_ERROR_GETTER;

namespace {

  namespace sb = sciforge::binding;

  constexpr long long kThreshold = 4096; // consumer-owned policy; substrate owns the mechanism

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
      return sb::set_cpp_error(sciforge_module_error());
    }
  }

  PyObject* raise_bad_alloc(PyObject* /*self*/,
                            PyObject* /*args*/)
  {
    try {
      throw std::bad_alloc();
    } catch (...) {
      return sb::set_cpp_error(sciforge_module_error());
    }
  }

  PyObject* raise_unknown(PyObject* /*self*/,
                          PyObject* /*args*/)
  {
    try {
      throw 42;    // not a std::exception -> the "internal error" branch
    } catch (...) {
      return sb::set_cpp_error(sciforge_module_error());
    }
  }

  PyObject* compute(PyObject* /*self*/,
                    PyObject* args)
  {
    long long size = 0;
    if (PyArg_ParseTuple(args, "L", &size) == 0) {
      return nullptr;
    }
    long long result = 0;
    {
      sb::scoped_gil_release release(size > kThreshold);
      result = work(size > 0 ? size : 0);
    }
    return PyLong_FromLongLong(result);
  }

  PyObject* release_then_throw(PyObject* /*self*/,
                               PyObject* /*args*/)
  {
    try {
      sb::gil_release release;
      throw std::runtime_error("thrown while the GIL was released");
    } catch (...) {
      return sb::set_cpp_error(sciforge_module_error());
    }
  }

  // --------------------------------------------------------------------------- N2
  bool        is_even(long long n)
  {
    return n % 2 == 0;
  }

  double      scale(double    x,
                    long long k)
  {
    return x * static_cast<double>(k);
  }

  std::string echo(std::string s)
  {
    return s;
  }

  std::string sv_echo(std::string_view s)
  {
    return std::string(s);
  }                                                                 // A10

  Py_ssize_t  blen(sb::bytes_view b)
  {
    return b.size;
  }

  std::vector<long long> upto(long long n)
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
  }                                            // pass-through (refcount test)

  void      noop()
  {
  }                                            // void -> None

  long long boom()
  {
    throw std::runtime_error("boom via dispatch");
  }

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

  // --------------------------------------------------------------------------- N3a
  // Keyword/optional arg-syntax: a scalar default, a const char* default, and the
  // PyObject* = None sentinel.
  bool        greater(long long x,
                      long long n)
  {
    return x > n;
  }                                                                   // arg("x"), arg("n") = 0

  std::string suffixed(std::string s,
                       std::string suffix)
  {
    return s + suffix;
  }                                                                    // arg("s"), arg("suffix") = "!"

  bool        defaulted_to_none(PyObject* x,
                                PyObject* extra)                       // arg("x"), arg("extra") = none
  {
    (void)x;
    return extra == Py_None;
  }

  // str-or-None optional via caster<std::optional<std::string>>: the byte length of opt
  // when given, else of s (the None fallback). arg("opt") = none materializes Py_None, so
  // an absent or explicit-None opt becomes std::nullopt.
  long long   maybe_len(std::string                s,
                        std::optional<std::string> opt)
  {
    return static_cast<long long>(opt ? opt->size() : s.size());
  }

  // caster<std::tuple<...>> (return side): a heterogeneous 2-tuple and a homogeneous
  // 3-tuple (the arities scinum's qr/eigh/svd need). Each element goes through its own caster.
  std::tuple<long long, std::string> classify(long long n)
  {
    return {n, n % 2 == 0 ? "even" : "odd"};
  }

  std::tuple<long long, long long, long long> triple(long long n)
  {
    return {n, n + 1, n + 2};
  }

  // --------------------------------------------------------------------------- N3b
  // A plain C++ type wrapped as a heap type by class_<Widget>. The methods/properties are
  // free functions whose first parameter is the unwrapped instance (no member pointers).
  struct Widget {
    long long w;
    long long h;
  };

  long long   area(const Widget& g)             // method:  w.area()
  {
    return g.w * g.h;
  }

  long long   width(const Widget& g)            // property: w.width
  {
    return g.w;
  }

  PyObject*   widget_dims(const Widget& g)      // a computed PyObject* property (a tuple)
  {
    return Py_BuildValue("(LL)", g.w, g.h);
  }

  Widget      make_widget(long long w,          // module fn -> Widget (exercises wrap)
                          long long h)
  {
    return Widget {w, h};
  }

  long long   widget_perimeter(const Widget& g) // module fn taking Widget (exercises unwrap)
  {
    return 2 * (g.w + g.h);
  }

  PyObject*   widget_describe(PyObject* self,   // a .raw method (the escape hatch)
                              PyObject* /*args*/)
  {
    // self is a Widget instance (the method lives on the Widget type); read it directly.
    const Widget& g = *reinterpret_cast<sb::wrapper<Widget>*>(self)->held;
    return PyUnicode_FromFormat("Widget(%lld, %lld)", g.w, g.h);
  }

  // A Python-constructible wrapped type via .def_init (mirrors scinum Tensor's arg shape:
  // a required arg, an optional-None arg through the optional caster, and a str default).
  struct Vec {
    long long   x;
    long long   scale;
    std::string label;
  };

  Vec         make_vec(long long                  x,        // Vec(x, scale=None, label="v")
                       std::optional<long long>   scale,
                       std::string                label)
  {
    return Vec {x, scale.value_or(1), label};
  }

  long long   vec_total(const Vec& v)
  {
    return v.x * v.scale;
  }

  std::string vec_label(const Vec& v)
  {
    return v.label;
  }
}  // namespace

// The handle caster (the documented per-binding pattern).
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

// The wrapped-type casters (one line per class_<T>).
SCIFORGE_WRAPPED(Widget)
SCIFORGE_WRAPPED(Vec)

SCIFORGE_MODULE(_demo, "bindingdemo.error", m)
{
  // N1 — manual PyCFunctions through m.raw.
  m.raw("raise_runtime", raise_runtime, METH_NOARGS, "Throw std::runtime_error -> module error.");
  m.raw("raise_bad_alloc", raise_bad_alloc, METH_NOARGS, "Throw std::bad_alloc -> MemoryError.");
  m.raw("raise_unknown", raise_unknown, METH_NOARGS, "Throw a non-exception -> internal error.");
  m.raw("compute", compute, METH_VARARGS, "Sum i%%7 for i in range(size); releases the GIL above the threshold.");
  m.raw("release_then_throw", release_then_throw, METH_NOARGS, "Release the GIL then throw; the RAII must restore it.");
  m.raw("make_handle", make_handle, METH_VARARGS, "Build a bindingdemo.handle capsule from an int.");
  // N2 — plain C++ through m.def.
  m.def<&is_even>("is_even", "n -> n is even");
  m.def<&scale>("scale", "x, k -> x*k");
  m.def<&echo>("echo", "s -> s");
  m.def<&sv_echo>("sv_echo", "s -> s (via string_view)");
  m.def<&blen>("blen", "b -> len(b)");
  m.def<&upto>("upto", "n -> [0, n)");
  m.def<&identity>("identity", "o -> o (pass-through)");
  m.def<&noop>("noop", "-> None");
  m.def<&boom>("boom", "throw -> module error");
  m.def<&handle_value>("handle_value", "capsule -> value");
  // N3a — keyword/optional arg-syntax.
  m.def<&greater>("greater", "x, n=0 -> x > n", sb::arg("x"), sb::arg("n")                  = 0LL);
  m.def<&suffixed>("suffixed", "s, suffix='!' -> s+suffix", sb::arg("s"), sb::arg("suffix") = "!");
  m.def<&defaulted_to_none>("defaulted_to_none", "x, extra=None -> extra is None",
                            sb::arg("x"), sb::arg("extra") = sb::none);
  m.def<&maybe_len>("maybe_len", "s, opt=None -> len(opt) if given else len(s)",
                    sb::arg("s"), sb::arg("opt") = sb::none);
  m.def<&classify>("classify", "n -> (n, 'even'/'odd')");
  m.def<&triple>("triple", "n -> (n, n+1, n+2)");
  // N3b — a C++ type wrapped as a heap type, plus module functions that take/return it.
  m.type<Widget>("bindingdemo.Widget")
  .def<&area>("area", "area() -> w*h")
  .def_prop_ro<&width>("width", "the width")
  .def_prop_ro<&widget_dims>("dims", "(w, h) as a tuple — a computed PyObject* property")
  .raw("describe", widget_describe, METH_NOARGS, "a manual repr (the .raw escape hatch)");
  m.def<&make_widget>("make_widget", "w, h -> Widget");
  m.def<&widget_perimeter>("widget_perimeter", "Widget -> 2*(w+h)");
  // N3b — a Python-constructible type (.def_init with optional/keyword args).
  m.type<Vec>("bindingdemo.Vec")
  .def_init<&make_vec>(sb::arg("x"), sb::arg("scale") = sb::none, sb::arg("label") = "v")
  .def<&vec_total>("total", "x * scale")
  .def_prop_ro<&vec_label>("label", "the label");

  // Negative compile-time proof: a specs/arity mismatch must NOT compile (the def's
  // static_assert). greater() takes two parameters; one arg() spec is a footgun
  // (out-of-bounds defaults read + format/varargs disagreement). Flip to 1 to confirm
  // it fails with "the number of arg() specifications must match ...".
#if 0
  m.def<&greater>("greater_bad", "mismatch", sb::arg("x"));
#endif
}
