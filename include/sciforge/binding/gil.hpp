// SciForge binding substrate — GIL release (Limited-API, abi3-safe).
//
// RAII GIL release that restores the GIL on EVERY scope exit, including a C++
// exception propagating out — unlike the bare Py_BEGIN/END_ALLOW_THREADS macros,
// whose END is skipped on a throw, leaving the GIL released (undefined behaviour
// afterwards). That restore-on-throw guarantee is the load-bearing reason this is
// RAII. The substrate owns the mechanism; the release *policy* (the threshold value)
// stays with the consumer, which measures it per workload.
#pragma once

#include <Python.h>

namespace sciforge::binding {

// Release the GIL for the lifetime of the object; restore it on destruction (every
// path, including stack unwinding from a throw).
class gil_release {
public:
    gil_release() : saved_(PyEval_SaveThread()) {}
    ~gil_release() { PyEval_RestoreThread(saved_); }

    gil_release(const gil_release&)            = delete;
    gil_release& operator=(const gil_release&) = delete;
    gil_release(gil_release&&)                 = delete;
    gil_release& operator=(gil_release&&)      = delete;

private:
    PyThreadState* saved_;
};

// Release the GIL only when `above` is true (the consumer's threshold test); below
// it the GIL is simply kept — releasing/re-acquiring would dominate a tiny scan.
class scoped_gil_release {
public:
    explicit scoped_gil_release(bool above)
        : saved_(above ? PyEval_SaveThread() : nullptr)
    {
    }
    ~scoped_gil_release()
    {
        if (saved_ != nullptr) {
            PyEval_RestoreThread(saved_);
        }
    }

    scoped_gil_release(const scoped_gil_release&)            = delete;
    scoped_gil_release& operator=(const scoped_gil_release&) = delete;
    scoped_gil_release(scoped_gil_release&&)                 = delete;
    scoped_gil_release& operator=(scoped_gil_release&&)      = delete;

private:
    PyThreadState* saved_;
};

}  // namespace sciforge::binding
