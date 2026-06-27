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


# --------------------------------------------------------------------------- N3a
class ArgSyntaxTest(unittest.TestCase):
    def test_scalar_default(self):
        self.assertIs(bindingdemo.greater(5), True)         # n defaults to 0
        self.assertIs(bindingdemo.greater(5, 10), False)    # positional
        self.assertIs(bindingdemo.greater(5, n=10), False)  # keyword

    def test_str_default(self):
        self.assertEqual(bindingdemo.suffixed("hi"), "hi!")            # default '!'
        self.assertEqual(bindingdemo.suffixed("hi", "?"), "hi?")       # positional
        self.assertEqual(bindingdemo.suffixed("hi", suffix="?"), "hi?")  # keyword

    def test_none_sentinel(self):
        obj = object()
        self.assertIs(bindingdemo.defaulted_to_none(obj), True)            # extra defaults to None
        self.assertIs(bindingdemo.defaulted_to_none(obj, 5), False)        # positional
        self.assertIs(bindingdemo.defaulted_to_none(obj, extra=5), False)  # keyword

    def test_missing_required_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.greater()

    def test_unknown_keyword_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.greater(5, nope=1)

    def test_too_many_positional_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.greater(1, 2, 3)


# ------------------------------------------------------------------ optional (Part A)
class OptionalArgTest(unittest.TestCase):
    def test_present_str_positional(self):
        self.assertEqual(bindingdemo.maybe_len("abc", "wxyz"), 4)   # opt present -> len(opt)

    def test_present_str_keyword(self):
        self.assertEqual(bindingdemo.maybe_len("abc", opt="wxyz"), 4)

    def test_absent_is_nullopt(self):
        self.assertEqual(bindingdemo.maybe_len("abc"), 3)           # opt absent -> nullopt -> len(s)

    def test_explicit_none_positional_is_nullopt(self):
        self.assertEqual(bindingdemo.maybe_len("abc", None), 3)

    def test_explicit_none_keyword_is_nullopt(self):
        self.assertEqual(bindingdemo.maybe_len("abc", opt=None), 3)

    def test_wrong_type_is_typeerror(self):
        with self.assertRaises(TypeError):  # opt is neither str nor None -> caster<str> trips
            bindingdemo.maybe_len("abc", 5)


# ------------------------------------------------------------------ tuple caster (S2)
class TupleCasterTest(unittest.TestCase):
    def test_heterogeneous_2_tuple(self):
        result = bindingdemo.classify(4)
        self.assertIsInstance(result, tuple)
        self.assertEqual(result, (4, "even"))
        self.assertIsInstance(result[0], int)
        self.assertIsInstance(result[1], str)

    def test_odd(self):
        self.assertEqual(bindingdemo.classify(3), (3, "odd"))

    def test_3_tuple(self):                          # the svd arity
        result = bindingdemo.triple(10)
        self.assertIsInstance(result, tuple)
        self.assertEqual(result, (10, 11, 12))


# ------------------------------------------------------------------ class_ (N3b)
class WidgetTest(unittest.TestCase):
    def test_make_returns_wrapped_type(self):       # wrap (to_python)
        w = bindingdemo.make_widget(3, 4)
        self.assertIsInstance(w, bindingdemo.Widget)

    def test_method_self_unwrap(self):              # method dispatch (self -> T&)
        self.assertEqual(bindingdemo.make_widget(3, 4).area(), 12)

    def test_property_ro(self):                     # def_prop_ro
        self.assertEqual(bindingdemo.make_widget(3, 4).width, 3)

    def test_property_is_read_only(self):
        w = bindingdemo.make_widget(3, 4)
        with self.assertRaises(AttributeError):
            w.width = 9

    def test_raw_method(self):                      # the .raw escape hatch
        self.assertEqual(bindingdemo.make_widget(3, 4).describe(), "Widget(3, 4)")

    def test_module_fn_unwraps_arg(self):           # unwrap (from_python) on an arg
        self.assertEqual(bindingdemo.widget_perimeter(bindingdemo.make_widget(3, 4)), 14)

    def test_wrong_type_to_module_fn_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.widget_perimeter("not a widget")

    def test_direct_instantiation_disallowed(self):
        with self.assertRaises(TypeError):
            bindingdemo.Widget()


# ------------------------------------------------------------------ def_init (S3a)
class DefInitTest(unittest.TestCase):
    def test_required_only_defaults(self):          # Vec(x) -> scale=None->1, label="v"
        v = bindingdemo.Vec(3)
        self.assertIsInstance(v, bindingdemo.Vec)
        self.assertEqual(v.total(), 3)
        self.assertEqual(v.label, "v")

    def test_positional(self):                       # Vec(x, scale)
        self.assertEqual(bindingdemo.Vec(3, 5).total(), 15)

    def test_keyword(self):                          # Vec(x, scale=, label=)
        v = bindingdemo.Vec(3, scale=5, label="hi")
        self.assertEqual(v.total(), 15)
        self.assertEqual(v.label, "hi")

    def test_keyword_skips_optional(self):           # label given, scale omitted -> None->1
        self.assertEqual(bindingdemo.Vec(4, label="z").total(), 4)

    def test_explicit_none_scale(self):              # scale=None -> optional caster -> 1
        self.assertEqual(bindingdemo.Vec(7, scale=None).total(), 7)

    def test_missing_required_is_typeerror(self):
        with self.assertRaises(TypeError):
            bindingdemo.Vec()

    def test_reinit_is_tolerated(self):              # __init__ re-callable (held deleted first)
        v = bindingdemo.Vec(3, 5)
        v.__init__(2, 4)
        self.assertEqual(v.total(), 8)


if __name__ == "__main__":
    unittest.main()
