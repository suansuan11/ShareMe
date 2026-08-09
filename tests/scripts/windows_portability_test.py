#!/usr/bin/env python3

import unittest
from pathlib import Path


class WindowsPortabilityTest(unittest.TestCase):
    def setUp(self) -> None:
        self.repo_root = Path(__file__).parents[2]

    def test_drift_scenario_uses_standard_cxx20_integer_arithmetic(self):
        source = (
            self.repo_root
            / "client"
            / "tools"
            / "rtc_demo"
            / "drift_scenario.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("__int128", source)


if __name__ == "__main__":
    unittest.main()
