/* Minimal Py_LIMITED_API (abi3) extension: one no-arg function returning 42.
 * Py_LIMITED_API is set on the compiler command line by setup.py's define_macros,
 * so it takes effect before <Python.h> is included here. */
#include <Python.h>

static PyObject *demo_answer(PyObject *self, PyObject *args)
{
    (void)self;
    (void)args;
    return PyLong_FromLong(42);
}

static PyMethodDef demo_methods[] = {
    {"answer", demo_answer, METH_NOARGS, "Return 42."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef demo_module = {
    PyModuleDef_HEAD_INIT, "_demo", NULL, -1, demo_methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit__demo(void)
{
    return PyModule_Create(&demo_module);
}
