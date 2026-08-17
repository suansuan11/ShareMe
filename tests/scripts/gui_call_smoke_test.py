#!/usr/bin/env python3

import importlib.util
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock


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
                  include_object_markers: bool = True,
                  include_advanced_action_marker: bool = True,
                  extra_output: str = "") -> Path:
        demo = directory / "fake-demo.py"
        demo.write_text(
            "#!/usr/bin/env python3\n"
            "import struct, sys, time, zlib\n"
            "state = sys.argv[sys.argv.index('--gui-smoke-state') + 1] "
            "if '--gui-smoke-state' in sys.argv else ''\n"
            f"fail = {fail_state!r}\n"
            f"idle_seconds = {idle_seconds!r}\n"
            f"include_object_markers = {include_object_markers!r}\n"
            f"include_advanced_action_marker = {include_advanced_action_marker!r}\n"
            f"extra_output = {extra_output!r}\n"
            "if fail and state == fail: sys.exit(3)\n"
            "if state == 'call-host-actions':\n"
            " print('GUI_ACTION microphone=1 speaker=1 drawer=1 voice_panel=1 "
            "volume_rejected_restored=1 leave=1 page=home')\n"
            " if include_advanced_action_marker:\n"
            "  print('GUI_ACTION advanced_closed=1 advanced_expanded=1')\n"
            "else:\n"
            " print(f'GUI_STATE page={state} qml_loaded=1')\n"
            " markers = {\n"
            "  'home': ('GUI_OBJECT createRoomButton=1',\n"
            "           'GUI_OBJECT joinRoomButton=1',\n"
            "           'GUI_OBJECT recentRoomAction=1'),\n"
            "  'create': ('GUI_OBJECT preflightPrimaryButton=1',\n"
            "             'GUI_OBJECT qualityProfileControl=1',\n"
            "             'GUI_OBJECT microphoneIntentControl=1',\n"
            "             'GUI_OBJECT speakerIntentControl=1'),\n"
            "  'join': ('GUI_OBJECT roomCodeField=1',\n"
            "           'GUI_OBJECT preflightPrimaryButton=1',\n"
            "           'GUI_OBJECT microphoneIntentControl=1',\n"
            "           'GUI_OBJECT speakerIntentControl=1'),\n"
            "  'settings': ('GUI_OBJECT settingsDialog=1',),\n"
            "  'help': ('GUI_OBJECT helpDialog=1',),\n"
            "  'recovery': ('GUI_OBJECT recoverySurface=1',),\n"
            "  'call-host': (\n"
            "      'GUI_OBJECT callPage=1',\n"
            "      'GUI_OBJECT microphoneControl=1',\n"
            "      'GUI_OBJECT speakerControl=1',\n"
            "      'GUI_OBJECT detailsControl=1',\n"
            "      'GUI_OBJECT leaveControl=1',\n"
            "      'GUI_OBJECT shareControl=0',\n"
            "      'GUI_OBJECT connectionSection=1',\n"
            "      'GUI_OBJECT videoSection=1',\n"
            "      'GUI_OBJECT audioSection=1',\n"
            "      'GUI_OBJECT advancedSection=1'),\n"
            "  'call-host-details': (\n"
            "      'GUI_OBJECT callPage=1',\n"
            "      'GUI_OBJECT microphoneControl=1',\n"
            "      'GUI_OBJECT speakerControl=1',\n"
            "      'GUI_OBJECT detailsControl=1',\n"
            "      'GUI_OBJECT leaveControl=1',\n"
            "      'GUI_OBJECT shareControl=0',\n"
            "      'GUI_OBJECT connectionSection=1',\n"
            "      'GUI_OBJECT videoSection=1',\n"
            "      'GUI_OBJECT audioSection=1',\n"
            "      'GUI_OBJECT advancedSection=1',\n"
            "      'GUI_OBJECT copyToastSurface=1'),\n"
            "  'call-host-details-copy': (\n"
            "      'GUI_OBJECT callPage=1',\n"
            "      'GUI_OBJECT microphoneControl=1',\n"
            "      'GUI_OBJECT speakerControl=1',\n"
            "      'GUI_OBJECT detailsControl=1',\n"
            "      'GUI_OBJECT leaveControl=1',\n"
            "      'GUI_OBJECT shareControl=0',\n"
            "      'GUI_OBJECT connectionSection=1',\n"
            "      'GUI_OBJECT videoSection=1',\n"
            "      'GUI_OBJECT audioSection=1',\n"
            "      'GUI_OBJECT advancedSection=1',\n"
            "      'GUI_OBJECT copyToastSurface=1'),\n"
            "  'call-viewer': (\n"
            "      'GUI_OBJECT callPage=1',\n"
            "      'GUI_OBJECT microphoneControl=1',\n"
            "      'GUI_OBJECT speakerControl=1',\n"
            "      'GUI_OBJECT detailsControl=1',\n"
            "      'GUI_OBJECT leaveControl=1',\n"
            "      'GUI_OBJECT shareControl=0',\n"
            "      'GUI_OBJECT connectionSection=1',\n"
            "      'GUI_OBJECT videoSection=1',\n"
            "      'GUI_OBJECT audioSection=1',\n"
            "      'GUI_OBJECT advancedSection=1'),\n"
            " }.get(state, ())\n"
            " if include_object_markers:\n"
            "  for marker in markers: print(marker)\n"
            " if state == 'recovery':\n"
            "  for marker in (\n"
            "   'GUI_RECOVERY_TITLE category=permission-denied title=\\u9700\\u8981\\u68c0\\u67e5\\u6743\\u9650',\n"
            "   'GUI_RECOVERY_TITLE category=invalid-room title=\\u65e0\\u6cd5\\u52a0\\u5165\\u8fd9\\u4e2a\\u623f\\u95f4',\n"
            "   'GUI_RECOVERY_TITLE category=screen-capture title=\\u5c4f\\u5e55\\u5171\\u4eab\\u4e0d\\u53ef\\u7528',\n"
            "   'GUI_RECOVERY_TITLE category=audio-device title=\\u58f0\\u97f3\\u8bbe\\u5907\\u4e0d\\u53ef\\u7528',\n"
            "   'GUI_RECOVERY_TITLE category=connection-lost title=\\u8fde\\u63a5\\u672a\\u5efa\\u7acb',\n"
            "   'GUI_RECOVERY_TITLE category=ICE-failed title=\\u8fde\\u63a5\\u672a\\u5efa\\u7acb',\n"
            "   'GUI_RECOVERY_TITLE category=timed out title=\\u8fde\\u63a5\\u672a\\u5efa\\u7acb',\n"
            "   'GUI_RECOVERY_TITLE category=generic-failure title=\\u901a\\u8bdd\\u6682\\u65f6\\u65e0\\u6cd5\\u7ee7\\u7eed',\n"
            "  ): print(marker)\n"
            " if state == 'call-host-details-copy':\n"
            "  print('GUI_LAYOUT toast_visible=1 toast_above_dock=1')\n"
            "if extra_output:\n"
            " for line in extra_output.splitlines(): print(line)\n"
            "if '--gui-screenshot' in sys.argv:\n"
            " path = sys.argv[sys.argv.index('--gui-screenshot') + 1]\n"
            " size = sys.argv[sys.argv.index('--gui-window-size') + 1]\n"
            " width, height = map(int, size.split('x'))\n"
            " raw = b''.join(b'\\x00' + b'\\x00\\x00\\x00' * width for _ in range(height))\n"
            " def chunk(kind, data):\n"
            "  body = kind + data\n"
            "  return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))\n"
            " png = b'\\x89PNG\\r\\n\\x1a\\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')\n"
            " open(path, 'wb').write(png)\n"
            " print('GUI_SCREENSHOT saved=1 preferences_ephemeral=1 recent_room_empty=1')\n"
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

    def test_all_eleven_probe_states_are_enforced(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(Path(temporary))
            results = self.runner.run_probes(
                demo, self.runner.GUI_PROBE_STATES, 2.0
            )
            self.assertEqual(
                [item.state for item in results],
                list(self.runner.GUI_PROBE_STATES),
            )
            self.assertEqual(
                len(results), self.runner.GUI_SMOKE_PROBE_COUNT
            )

    def test_visual_matrix_covers_default_and_compact_surfaces(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = self.make_demo(root)
            captures = self.runner.capture_visual_matrix(demo, root / "shots", 2.0)
            self.assertEqual(len(captures), len(self.runner.GUI_VISUAL_MATRIX))
            self.assertIn({"state": "call-host-details",
                           "file": "call-details-compact.png",
                           "width": 760, "height": 520}, captures)
            self.assertIn({"state": "call-host-details-copy",
                           "file": "call-details-copy-compact.png",
                           "width": 760, "height": 520}, captures)

    def test_gui_object_marker_requires_an_exact_line(self):
        completed = self.runner.subprocess.CompletedProcess(
            args=["fake-demo"],
            returncode=0,
            stdout=("GUI_STATE page=home qml_loaded=1\n"
                    "GUI_OBJECT createRoomButton=10\n"
                    "GUI_OBJECT joinRoomButton=1\n"
                    "GUI_OBJECT recentRoomAction=1\n"),
            stderr="",
        )
        with mock.patch.object(
            self.runner.subprocess, "run", return_value=completed
        ):
            with self.assertRaisesRegex(
                self.runner.GuiSmokeFailure, "probe-contract:home"
            ):
                self.runner.run_probes(Path("fake-demo"), ("home",), 2.0)

    def test_gui_action_marker_requires_an_exact_line(self):
        completed = self.runner.subprocess.CompletedProcess(
            args=["fake-demo"],
            returncode=0,
            stdout=(
                "GUI_ACTION microphone=1 speaker=1 drawer=1 voice_panel=1 "
                "volume_rejected_restored=1 leave=1 page=home\n"
                "GUI_ACTION advanced_closed=1 advanced_expanded=10\n"
            ),
            stderr="",
        )
        with mock.patch.object(
            self.runner.subprocess, "run", return_value=completed
        ):
            with self.assertRaisesRegex(
                self.runner.GuiSmokeFailure, "probe-contract:call-host-actions"
            ):
                self.runner.run_probes(
                    Path("fake-demo"), ("call-host-actions",), 2.0
                )

    def test_recovery_marker_requires_an_exact_line(self):
        recovery_markers = list(self.runner._GUI_RECOVERY_MARKERS)
        recovery_markers[0] += " extra"
        completed = self.runner.subprocess.CompletedProcess(
            args=["fake-demo"],
            returncode=0,
            stdout=(
                "GUI_STATE page=recovery qml_loaded=1\n"
                "GUI_OBJECT recoverySurface=1\n"
                + "\n".join(recovery_markers)
                + "\n"
            ),
            stderr="",
        )
        with mock.patch.object(
            self.runner.subprocess, "run", return_value=completed
        ):
            with self.assertRaisesRegex(
                self.runner.GuiSmokeFailure, "probe-contract:recovery"
            ):
                self.runner.run_probes(Path("fake-demo"), ("recovery",), 2.0)

    def test_rejects_unsanitized_gui_output(self):
        for unsafe in (
            "kVTParameterErr",
            "HRESULT",
            "NSError",
            "ICE",
            "SDP",
            "/private/secret/shareme.exe",
            "password=super-secret",
        ):
            with self.subTest(unsafe=unsafe):
                with tempfile.TemporaryDirectory() as temporary:
                    demo = self.make_demo(
                        Path(temporary),
                        extra_output=f"GUI_STATE detail={unsafe}",
                    )
                    with self.assertRaisesRegex(
                        self.runner.GuiSmokeFailure, "probe-sanitized:home"
                    ):
                        self.runner.run_probes(demo, ("home",), 2.0)

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

    def test_requires_advanced_action_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            demo = self.make_demo(
                Path(temporary), include_advanced_action_marker=False
            )
            with self.assertRaisesRegex(
                self.runner.GuiSmokeFailure, "probe-contract:call-host-actions"
            ):
                self.runner.run_probes(demo, ("call-host-actions",), 2.0)

    def test_probe_subprocess_uses_strict_utf8_decoding(self):
        completed = self.runner.subprocess.CompletedProcess(
            args=["fake-demo"],
            returncode=0,
            stdout=("GUI_STATE page=home qml_loaded=1\n"
                    "GUI_OBJECT createRoomButton=1\n"
                    "GUI_OBJECT joinRoomButton=1\n"
                    "GUI_OBJECT recentRoomAction=1\n"),
            stderr="",
        )
        with mock.patch.object(
            self.runner.subprocess, "run", return_value=completed
        ) as run:
            self.runner.run_probes(Path("fake-demo"), ("home",), 2.0)
        self.assertEqual(run.call_args.kwargs.get("encoding"), "utf-8")
        self.assertEqual(run.call_args.kwargs.get("errors"), "strict")

    def test_timeout_preserves_completed_probe_results(self):
        completed = self.runner.subprocess.CompletedProcess(
            args=["fake-demo"],
            returncode=0,
            stdout=("GUI_STATE page=home qml_loaded=1\n"
                    "GUI_OBJECT createRoomButton=1\n"
                    "GUI_OBJECT joinRoomButton=1\n"
                    "GUI_OBJECT recentRoomAction=1\n"),
            stderr="",
        )
        timeout = self.runner.subprocess.TimeoutExpired(
            cmd=["fake-demo"], timeout=2.0
        )
        with mock.patch.object(
            self.runner.subprocess, "run", side_effect=[completed, timeout]
        ):
            with self.assertRaisesRegex(
                self.runner.GuiSmokeFailure, "^timeout$"
            ) as raised:
                self.runner.run_probes(
                    Path("fake-demo"), ("home", "create"), 2.0
                )
        self.assertEqual(
            [item.state for item in raised.exception.partial], ["home"]
        )

    def test_atomic_artifact_contains_no_temporary_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "result.json"
            self.runner.atomic_write_json(artifact, {"schema": "gui-call-smoke-v1"})
            self.assertEqual(json.loads(artifact.read_text())["schema"],
                             "gui-call-smoke-v1")
            self.assertEqual(list(Path(temporary).glob("*.tmp")), [])

    def test_strict_decode_failure_writes_atomic_failed_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = self.make_demo(root)
            artifact = root / "result.json"
            artifact.write_text('{"status": "old"}\n', encoding="utf-8")
            decode_error = UnicodeDecodeError(
                "utf-8", b"\xff", 0, 1, "invalid start byte"
            )
            with mock.patch.object(
                self.runner.subprocess, "run", side_effect=decode_error
            ), mock.patch.object(
                self.runner.sys,
                "argv",
                [
                    "run_gui_call_smoke.py",
                    "--demo", str(demo),
                    "--artifact", str(artifact),
                ],
            ), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(self.runner.main(), 1)
            payload = json.loads(artifact.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "failed")
            self.assertEqual(payload["failure"], "decode-error")
            self.assertEqual(list(root.glob("*.tmp")), [])

    def test_idle_failure_preserves_completed_gui_probe_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = self.make_demo(root)
            artifact = root / "result.json"
            artifact.write_text('{"status": "old"}\n', encoding="utf-8")
            probes = [
                self.runner.ProbeResult(state, True, 1)
                for state in self.runner.GUI_PROBE_STATES
            ]
            with mock.patch.object(
                self.runner, "run_probes", return_value=probes
            ), mock.patch.object(
                self.runner,
                "sample_idle_process",
                side_effect=self.runner.GuiSmokeFailure(
                    "process-output-malformed", []
                ),
            ), mock.patch.object(
                self.runner.sys,
                "argv",
                [
                    "run_gui_call_smoke.py",
                    "--demo", str(demo),
                    "--artifact", str(artifact),
                ],
            ), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(self.runner.main(), 1)
            payload = json.loads(artifact.read_text(encoding="utf-8"))
            self.assertEqual(payload["failure"], "process-output-malformed")
            self.assertEqual(
                [item["state"] for item in payload["probes"]],
                list(self.runner.GUI_PROBE_STATES),
            )

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
