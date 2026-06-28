"""Selftest for the runner layer: the collector and the shared duration formatter."""

import unittest

from sciforge.bench import collect, fmt


class FmtTest(unittest.TestCase):
    def test_unit_by_magnitude(self):
        self.assertEqual(fmt(5e-9).strip(), "5 ns")
        self.assertEqual(fmt(2e-6).strip(), "2.0 µs")
        self.assertEqual(fmt(3e-3).strip(), "3.00 ms")

    def test_threshold_boundaries(self):
        # < 1e-6 is ns; [1e-6, 1e-3) is µs; >= 1e-3 is ms.
        self.assertTrue(fmt(9.9e-7).endswith("ns"))
        self.assertTrue(fmt(1e-6).endswith("µs"))
        self.assertTrue(fmt(1e-3).endswith("ms"))

    def test_width_pads_with_spaces_without_changing_the_number(self):
        wide, bare = fmt(2e-6, 8), fmt(2e-6)
        self.assertEqual(wide.strip(), bare.strip())   # same number + unit
        self.assertGreater(len(wide), len(bare))       # width 8 adds leading spaces
        self.assertTrue(wide.startswith(" "))


class CollectTest(unittest.TestCase):
    def test_returns_a_raw_sample_case(self):
        case = collect("noop", lambda: None, samples=5)
        self.assertEqual(case.name, "noop")
        self.assertEqual(case.unit, "s")
        self.assertEqual(len(case.samples), 5)         # one per requested sample
        self.assertTrue(all(s >= 0.0 for s in case.samples))
        self.assertEqual(case.result, None)            # collect measures, derives nothing


if __name__ == "__main__":
    unittest.main()
