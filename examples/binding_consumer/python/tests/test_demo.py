import sys
import unittest

import bindingdemo


def _expected(n):
    return sum(i % 7 for i in range(n))


# --------------------------------------------------------------------------- N1
class SetCppErrorTest(unittest.TestCase):
    def test_runtime_error_maps_to_module_error(self):
        with self.assertRaises(bindingdemo.error) as cm:
            bindingdemo.raise_runtime()
        self.assertIn("boom from C++", str(cm.exception))

    def test_bad_alloc_maps_to_memoryerror(self):
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
        with self.assertRaises(bindingdemo.error):
            bindingdemo.release_then_throw()
        # The interpreter must still be usable (the RAII restored the GIL on the throw).
        self.assertEqual(bindingdemo.compute(3), _expected(3))


# --------------------------------------------------------------------------- N2
class CasterTest(unittest.TestCase):
    def test_int_and_bool(self):
        self.assertIs(bindingdemo.is_even(4), True)
        self.assertIs(bindingdemo.is_even(3), False)

    def test_double_and_int(self):
        self.assertEqual(bindingdemo.scale(2.5, 3), 7.5)

    def test_str(self):
        self.assertEqual(bindingdemo.echo("hi"), "hi")

    def test_string_view_roundtrip(self):  # A10: str -> view -> str in one call
        self.assertEqual(bindingdemo.sv_echo("hello"), "hello")

    def test_bytes_only(self):
        self.assertEqual(bindingdemo.blen(b"abc"), 3)
        with self.assertRaises(TypeError):  # A2: str is not bytes
            bindingdemo.blen("abc")

    def test_vector_to_list(self):
        self.assertEqual(bindingdemo.upto(3), [0, 1, 2])

    def test_void_returns_none(self):
        self.assertIsNone(bindingdemo.noop())

    def test_throw_maps_to_module_error(self):  # the N1 bridge fires through the dispatch
        with self.assertRaises(bindingdemo.error) as cm:
            bindingdemo.boom()
        self.assertIn("boom via dispatch", str(cm.exception))


class DispatchErrorTest(unittest.TestCase):
    def test_wrong_type_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.is_even("not an int")

    def test_wrong_arity_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.is_even()          # too few
        with self.assertRaises(TypeError):
            bindingdemo.is_even(1, 2)      # too many


class PassThroughTest(unittest.TestCase):
    def test_identity_and_refcount(self):  # A4: borrowed in / new ref out, no leak
        obj = object()
        before = sys.getrefcount(obj)
        result = bindingdemo.identity(obj)
        self.assertIs(result, obj)
        del result
        self.assertEqual(sys.getrefcount(obj), before)


class CapsuleTest(unittest.TestCase):
    def test_capsule_caster_roundtrip(self):
        capsule = bindingdemo.make_handle(7)
        self.assertEqual(bindingdemo.handle_value(capsule), 7)

    def test_wrong_capsule_is_typeerror(self):
        with self.assertRaises(TypeError):  # not a capsule -> cast_error -> TypeError
            bindingdemo.handle_value(object())


if __name__ == "__main__":
    unittest.main()
