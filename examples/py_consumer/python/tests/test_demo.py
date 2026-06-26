import unittest

import pydemo


class DemoTest(unittest.TestCase):
    def test_answer(self):
        self.assertEqual(pydemo.answer(), 42)


if __name__ == "__main__":
    unittest.main()
