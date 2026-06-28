"""Selftest for the exchange schema: a JSON round-trip preserves raw data and free fields."""

import os
import tempfile
import unittest

from sciforge.bench import Case, Run, load_run, run_from_json, run_to_json, write_run


class RoundTripTest(unittest.TestCase):
    def _sample_run(self):
        return Run(
            meta={"tool": "selftest", "host": "ci", "samples": 40},
            cases=[
                Case(name="alpha", unit="s", samples=[0.1, 0.2, 0.3],
                     extra={"family": "scaling", "size": 1024, "engine": "A"}),
                Case(name="beta", unit="ns/B", samples=[6.3, 6.4], result="ok"),
            ],
        )

    def test_dict_round_trip_preserves_everything(self):
        run = self._sample_run()
        back = run_from_json(run_to_json(run))
        self.assertEqual(back.meta, run.meta)
        self.assertEqual(len(back.cases), 2)

        alpha = back.cases[0]
        self.assertEqual(alpha.name, "alpha")
        self.assertEqual(alpha.unit, "s")
        self.assertEqual(alpha.samples, [0.1, 0.2, 0.3])
        self.assertIsNone(alpha.result)
        # The free domain fields survive verbatim, as first-class siblings.
        self.assertEqual(alpha.extra, {"family": "scaling", "size": 1024, "engine": "A"})

        beta = back.cases[1]
        self.assertEqual(beta.result, "ok")
        self.assertEqual(beta.extra, {})

    def test_free_fields_are_not_nested(self):
        obj = run_to_json(self._sample_run())
        # A domain field is a sibling of name/unit/samples, not buried under "extra".
        self.assertEqual(obj["cases"][0]["family"], "scaling")
        self.assertNotIn("extra", obj["cases"][0])

    def test_file_round_trip(self):
        run = self._sample_run()
        handle, path = tempfile.mkstemp(suffix=".json")
        os.close(handle)
        try:
            write_run(run, path)
            back = load_run(path)
        finally:
            os.remove(path)
        self.assertEqual(run_to_json(back), run_to_json(run))


if __name__ == "__main__":
    unittest.main()
