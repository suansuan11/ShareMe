#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import unittest
from pathlib import Path


class GuiQmlContractTest(unittest.TestCase):
    demo = Path()

    def run_state(self, state: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        return subprocess.run(
            [str(self.demo), "--gui-smoke-state", state],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
            env=environment,
        )

    def assert_clean_state(self, state: str) -> None:
        result = self.run_state(state)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"GUI_STATE page={state} qml_loaded=1", result.stdout)
        for failure in (
            "TypeError:",
            "ReferenceError:",
            "is not a type",
            "failed to load component",
            "Binding loop",
        ):
            self.assertNotIn(failure, result.stderr)

    def test_home_loads_without_qml_errors(self):
        self.assert_clean_state("home")

    def test_create_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("create")

    def test_join_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("join")

    def test_unknown_state_fails_closed(self):
        result = self.run_state("unknown")
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("GUI_STATE", result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    args, unittest_args = parser.parse_known_args()
    GuiQmlContractTest.demo = args.demo.resolve()
    unittest.main(argv=[sys.argv[0], *unittest_args])
    return 0


if __name__ == "__main__":
    sys.exit(main())
