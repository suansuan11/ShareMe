import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO = Path(__file__).resolve().parents[2]
EXPECTED_REVISION = "5ad58d70eea10785fab05ba4150e2fe22ecc7f97"
sys.path.insert(0, str(REPO))

from scripts.bootstrap_webrtc import (
    _prepare,
    create_plan,
    load_lock,
    main,
    make_manifest,
)


class BootstrapWebRtcTest(unittest.TestCase):
    def test_lock_uses_full_expected_revision(self):
        lock = load_lock(REPO / "deps/webrtc.lock.json")

        self.assertEqual(lock["revision"], EXPECTED_REVISION)
        self.assertEqual(
            lock["targets"],
            ["webrtc", "modules/audio_device:test_audio_device_module"],
        )

    def test_plan_keeps_checkout_outside_repository(self):
        with self.assertRaisesRegex(ValueError, "outside the repository"):
            create_plan(REPO, REPO / ".cache")

    def test_manifest_records_abi_inputs(self):
        manifest = make_manifest(
            revision=EXPECTED_REVISION,
            system="Darwin",
            architecture="arm64",
            include_dir="/external/src",
            libraries=["/external/out/obj/libwebrtc.a"],
            compile_definitions=["WEBRTC_POSIX", "WEBRTC_MAC"],
            gn_args=["is_debug=false"],
        )

        self.assertEqual(manifest["revision"], EXPECTED_REVISION)
        self.assertEqual(manifest["system"], "Darwin")
        self.assertEqual(manifest["architecture"], "arm64")
        self.assertEqual(manifest["includeDir"], "/external/src")
        self.assertTrue(manifest["libraries"])
        self.assertEqual(
            manifest["compileDefinitions"], ["WEBRTC_POSIX", "WEBRTC_MAC"]
        )

    def test_load_lock_rejects_short_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            lock_path = Path(directory) / "webrtc.lock.json"
            lock_path.write_text(
                json.dumps(
                    {
                        "revision": "5ad58d7",
                        "targets": ["webrtc"],
                        "gnArgs": ["is_debug=false"],
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "40-character"):
                load_lock(lock_path)

    def test_load_lock_rejects_unknown_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            lock_path = Path(directory) / "webrtc.lock.json"
            lock_path.write_text(
                json.dumps(
                    {
                        "revision": EXPECTED_REVISION,
                        "targets": ["webrtc"],
                        "gnArgs": ["is_debug=false"],
                        "downloadIntoRepository": True,
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "unsupported keys"):
                load_lock(lock_path)

    def test_subprocess_failure_does_not_print_command_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            error_output = io.StringIO()
            failure = subprocess.CalledProcessError(
                1, ["gclient", "/private/dependency/path"]
            )

            with mock.patch(
                "scripts.bootstrap_webrtc._prepare", side_effect=failure
            ), contextlib.redirect_stderr(error_output):
                result = main(["--root", directory, "--prepare"])

            self.assertEqual(result, 1)
            self.assertNotIn("/private/dependency/path", error_output.getvalue())
            self.assertIn("preparing failed", error_output.getvalue())

    def test_prepare_uses_shallow_fetch_for_first_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            external_root = Path(directory)
            depot_tools = external_root / "depot_tools"
            (depot_tools / ".git").mkdir(parents=True)
            plan = create_plan(REPO, external_root)
            commands = []

            def fake_run(command, *, cwd, env):
                commands.append(list(command))
                if command[0] == "fetch":
                    checkout_root = Path(plan["checkoutRoot"])
                    (checkout_root / ".gclient").touch()
                    (Path(plan["sourceRoot"]) / ".git").mkdir(parents=True)

            with mock.patch("scripts.bootstrap_webrtc._run", side_effect=fake_run):
                _prepare(plan)

            self.assertIn(
                ["fetch", "--nohooks", "--no-history", "webrtc"], commands
            )


if __name__ == "__main__":
    unittest.main()
