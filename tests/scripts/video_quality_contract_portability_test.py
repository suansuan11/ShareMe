#!/usr/bin/env python3

import unittest
from pathlib import Path


class VideoQualityContractPortabilityTest(unittest.TestCase):
    def test_rational_comparison_uses_standard_cxx20(self):
        source = (
            Path(__file__).parents[2]
            / "client"
            / "core"
            / "src"
            / "video_quality_contract.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("__int128", source)


if __name__ == "__main__":
    unittest.main()
