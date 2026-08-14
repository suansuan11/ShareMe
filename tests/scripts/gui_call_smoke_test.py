#!/usr/bin/env python3

import importlib.util
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path


class GuiCallSmokeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = Path(__file__).parents[2] / "scripts" / "run_gui_call_smoke.py"
        spec = importlib.util.spec_from_file_location("gui_call_smoke", script)
        cls.runner = importlib.util.module_from_spec(spec)
        assert spec.loader
        sys.modules[spec.name] = cls.runner
        spec.loader.exec_module(cls.runner)

    def make_demo(self, directory: Path, fail_state: str = "",
                  idle_seconds: float = 0.0,
                  include_object_markers: bool = True) -> Path:
        demo = directory / "fake-demo.py"
        demo.write_text(
            "#!/usr/bin/env python3\n"
            "import sys, time\n"
            "state = sys.argv[sys.argv.index('--gui-smoke-state') + 1] "
            "if '--gui-smoke-state' in sys.argv else ''\n"
            f"fail = {fail_state!r}\n"
            f"idle_seconds = {idle_seconds!r}\n"
            f"include_object_markers = {include_object_markers!r}\n"
            "if fail and state == fail: sys.exit(3)\n"
            "if state == 'call-host-actions':\n"
            " print('GUI_ACTION microphone=1 speaker=1 drawer=1 voice_panel=1 "
            "volume_rejected_restored=1 leave=1 page=home')\n"
            "else:\n"
            " print(f'GUI_STATE page={state} qml_loaded=1')\n"
            " markers = {\n"
            "  'home': ('createRoomButton', 'joinRoomButton', 'recentRoomAction'),\n"
            "  'create': ('preflightPrimaryButton', 'qualityProfileControl'),\n"
            "  'join': ('roomCodeField', 'preflightPrimaryButton'),\n"
            " }.get(state, ())\n"
            " if include_object_markers:\n"
            "  for marker in markers: print(f'GUI_OBJECT {marker}=1')\n"
            "if not state: time.sleep(idle_seconds)\n",
            encoding="utf-8",
        )
        demo.chmod(demo.stat().st_mode | stat.S_IXUSR)
        return demo

    def test_runs_state_and_real_action_contracts(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(Path(temporary))
            results = self.runner.run_probes(
                demo, ("home", "create", "call-host-actions"), 2.0
            )
            self.assertEqual([item.state for item in results],
                             ["home", "create", "call-host-actions"])
            self.assertTrue(all(item.passed for item in results))

    def test_preserves_failure_category_and_partial_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(Path(temporary), "join")
            with self.assertRaisesRegex(self.runner.GuiSmokeFailure,
                                        "probe-exit") as raised:
                self.runner.run_probes(demo, ("home", "join"), 2.0)
            self.assertEqual(len(raised.exception.partial), 1)
            self.assertEqual(raised.exception.partial[0].state, "home")

    def test_requires_home_and_preflight_object_markers(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(
                Path(temporary), include_object_markers=False
            )
            with self.assertRaisesRegex(self.runner.GuiSmokeFailure,
                                        "probe-contract:home"):
                self.runner.run_probes(demo, ("home",), 2.0)

    def test_atomic_artifact_contains_no_temporary_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "result.json"
            self.runner.atomic_write_json(artifact, {"schema": "gui-call-smoke-v1"})
            self.assertEqual(json.loads(artifact.read_text())["schema"],
                             "gui-call-smoke-v1")
            self.assertEqual(list(Path(temporary).glob("*.tmp")), [])

    def test_idle_sampling_uses_injected_cross_platform_sampler(self):
        runner = self.runner
        samplers = []

        class FakeSampler:
            def __init__(self, pid: int):
                self.pid = pid
                self.index = 0
                self.closed = False
                samplers.append(self)

            def sample(self):
                values = ((2.5, 1_024), (7.5, 2_048))
                cpu_percent, rss_bytes = values[self.index % len(values)]
                self.index += 1
                return runner.ProcessSample(
                    monotonic_ms=self.index * 250,
                    cpu_percent=cpu_percent,
                    rss_bytes=rss_bytes,
                )

            def close(self):
                self.closed = True

        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(Path(temporary), idle_seconds=2.0)
            idle = self.runner.sample_idle_process(
                demo, 0.3, sampler_factory=FakeSampler
            )
        self.assertGreaterEqual(idle["sampleCount"], 1)
        self.assertGreater(idle["cpuMeanPercent"], 0.0)
        self.assertEqual(idle["rssMaxKiB"], 2)
        self.assertTrue(samplers[0].closed)

    def test_idle_sampling_closes_sampler_after_sampling_failure(self):
        runner = self.runner
        samplers = []

        class FailingSampler:
            def __init__(self, pid: int):
                self.closed = False
                samplers.append(self)

            def sample(self):
                raise runner.ProcessMetricsError("process-output-malformed")

            def close(self):
                self.closed = True

        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(Path(temporary), idle_seconds=2.0)
            with self.assertRaisesRegex(self.runner.GuiSmokeFailure,
                                        "process-output-malformed"):
                self.runner.sample_idle_process(
                    demo, 0.3, sampler_factory=FailingSampler
                )
        self.assertTrue(samplers[0].closed)


if __name__ == "__main__":
    unittest.main()
