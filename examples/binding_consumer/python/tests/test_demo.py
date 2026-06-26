import unittest

import bindingdemo


def _expected(n):
    return sum(i % 7 for i in range(n))


class SetCppErrorTest(unittest.TestCase):
    def test_runtime_error_maps_to_module_error(self):
        with self.assertRaises(bindingdemo.error) as cm:
            bindingdemo.raise_runtime()
        self.assertIn("boom from C++", str(cm.exception))

    def test_bad_alloc_maps_to_memoryerror(self):
        # The distinct bad_alloc branch -> MemoryError (not the module error).
        with self.assertRaises(MemoryError):
            bindingdemo.raise_bad_alloc()

    def test_unknown_maps_to_internal_error(self):
        with self.assertRaises(bindingdemo.error) as cm:
            bindingdemo.raise_unknown()
        self.assertIn("internal error", str(cm.exception))


class GilTest(unittest.TestCase):
    def test_below_threshold_keeps_gil(self):
        self.assertEqual(bindingdemo.compute(10), _expected(10))

    def test_above_threshold_releases_gil(self):
        self.assertEqual(bindingdemo.compute(5000), _expected(5000))

    def test_gil_restored_on_throw(self):
        # The exception must surface...
        with self.assertRaises(bindingdemo.error):
            bindingdemo.release_then_throw()
        # ...and the interpreter must still be usable, i.e. the RAII restored the GIL
        # during unwinding despite the throw (a bare BEGIN/END_ALLOW_THREADS would
        # have left it released and this next call would deadlock/crash).
        self.assertEqual(bindingdemo.compute(3), _expected(3))


if __name__ == "__main__":
    unittest.main()
