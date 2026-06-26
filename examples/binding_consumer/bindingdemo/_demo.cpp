// Real abi3 C++ binding fixture for SciForge's binding-substrate (N1) self-test.
// It does no real work — it exists to exercise sciforge::binding::set_cpp_error
// (all three branches), register_error, and the GIL helpers (above/below a
// threshold, and the load-bearing restore-on-throw). Py_LIMITED_API is set on the
// compiler command line by setup.py, so this compiles into one abi3 wheel.
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/gil.hpp>

#include <new>
#include <stdexcept>

namespace {

// The module's error type, created in module init via register_error.
PyObject* g_error = nullptr;

constexpr long long kThreshold = 4096;  // consumer-owned policy; substrate owns the mechanism

// A trivial deterministic workload so the GIL paths have something to run.
long long work(long long n)
{
    long long acc = 0;
    for (long long i = 0; i < n; ++i) {
        acc += i % 7;
    }
    return acc;
}

PyObject* raise_runtime(PyObject* /*self*/, PyObject* /*args*/)
{
    try {
        throw std::runtime_error("boom from C++");
    } catch (...) {
        return sciforge::binding::set_cpp_error(g_error);
    }
}

PyObject* raise_bad_alloc(PyObject* /*self*/, PyObject* /*args*/)
{
    try {
        throw std::bad_alloc();
    } catch (...) {
        return sciforge::binding::set_cpp_error(g_error);
    }
}

PyObject* raise_unknown(PyObject* /*self*/, PyObject* /*args*/)
{
    try {
        throw 42;  // not a std::exception -> the "internal error" branch
    } catch (...) {
        return sciforge::binding::set_cpp_error(g_error);
    }
}

// Release the GIL only above the threshold; both paths must compute the same result.
PyObject* compute(PyObject* /*self*/, PyObject* args)
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
PyObject* release_then_throw(PyObject* /*self*/, PyObject* /*args*/)
{
    try {
        sciforge::binding::gil_release release;
        throw std::runtime_error("thrown while the GIL was released");
    } catch (...) {
        return sciforge::binding::set_cpp_error(g_error);
    }
}

PyMethodDef methods[] = {
    {"raise_runtime",      raise_runtime,      METH_NOARGS,  "Throw std::runtime_error -> module error."},
    {"raise_bad_alloc",    raise_bad_alloc,    METH_NOARGS,  "Throw std::bad_alloc -> MemoryError."},
    {"raise_unknown",      raise_unknown,      METH_NOARGS,  "Throw a non-exception -> internal error."},
    {"compute",            compute,            METH_VARARGS, "Sum i%%7 for i in range(size); releases the GIL above the threshold."},
    {"release_then_throw", release_then_throw, METH_NOARGS,  "Release the GIL then throw; the RAII must restore it."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_demo", "SciForge binding-substrate (N1) self-test fixture.",
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
