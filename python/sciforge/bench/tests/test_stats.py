"""Selftest for the domain-free stats primitives (pure stdlib, no benchmark needed)."""

import statistics
import unittest

from sciforge.bench import (
    ascii_boxplot,
    ascii_ecdf,
    bootstrap_ci,
    geomean_ci,
    median_iqr,
    ratio_ci,
)


class BootstrapTest(unittest.TestCase):
    def test_constant_sample_has_a_degenerate_ci(self):
        # Every resample of a constant is that constant, so the CI collapses to [x, x].
        low, high = bootstrap_ci([5.0] * 8, statistics.fmean)
        self.assertEqual((low, high), (5.0, 5.0))

    def test_needs_at_least_one_sample(self):
        with self.assertRaises(ValueError):
            bootstrap_ci([], statistics.fmean)


class GeomeanTest(unittest.TestCase):
    def test_reciprocal_pair_is_unity(self):
        # geomean of [2, 1/2] = sqrt(2 * 0.5) = 1.0, with a CI that brackets it.
        point, low, high = geomean_ci([2.0, 0.5])
        self.assertAlmostEqual(point, 1.0, places=12)
        self.assertLessEqual(low, point)
        self.assertLessEqual(point, high)

    def test_needs_at_least_one_ratio(self):
        with self.assertRaises(ValueError):
            geomean_ci([])


class RatioTest(unittest.TestCase):
    def test_ratio_of_constant_medians(self):
        point, low, high = ratio_ci([4.0] * 5, [2.0] * 5)
        self.assertEqual((point, low, high), (2.0, 2.0, 2.0))


class MedianIqrTest(unittest.TestCase):
    def test_quartiles_exclusive_rule(self):
        median, q1, q3, iqr, minimum = median_iqr([1, 2, 3, 4, 5])
        self.assertEqual(median, 3)
        self.assertEqual((q1, q3), (1.5, 4.5))  # exclusive: median of halves, dropping the 3
        self.assertEqual(iqr, q3 - q1)
        self.assertEqual(minimum, 1)

    def test_single_sample(self):
        self.assertEqual(median_iqr([7.0]), (7.0, 7.0, 7.0, 0.0, 7.0))


class BoxplotTest(unittest.TestCase):
    def test_two_series_render_two_rows_plus_an_axis(self):
        out = ascii_boxplot([[1, 2, 3, 4], [2, 4, 6, 8]], ["a", "b"])
        lines = out.splitlines()
        self.assertEqual(len(lines), 3)        # two series rows + one shared axis row
        self.assertTrue(lines[0].startswith("a"))
        self.assertTrue(lines[1].startswith("b"))
        self.assertIn("█", lines[0])           # the median marker is drawn

    def test_empty_series_is_empty(self):
        self.assertEqual(ascii_boxplot([[], []], ["a", "b"]), "")


class EcdfTest(unittest.TestCase):
    def test_renders_a_grid_with_axis(self):
        out = ascii_ecdf([1, 2, 3, 4, 5], width=12, height=4)
        lines = out.splitlines()
        self.assertEqual(len(lines), 6)        # height rows + underline + label row
        self.assertIn("█", out)

    def test_empty_is_empty(self):
        self.assertEqual(ascii_ecdf([]), "")


if __name__ == "__main__":
    unittest.main()
