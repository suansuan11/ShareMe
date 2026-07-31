#!/usr/bin/env python3

import argparse
import importlib.util
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
from pathlib import Path

sys.dont_write_bytecode = True


def load_smoke_script(path: Path):
    spec = importlib.util.spec_from_file_location("shareme_signaled_smoke", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load smoke script")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SignaledCallSmokeTest(unittest.TestCase):
    smoke = None

    def test_occupied_health_port_is_rejected_before_server_start(self):
        occupied = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        occupied.bind(("127.0.0.1", 0))
        occupied.listen()
        occupied.settimeout(0.1)
        port = occupied.getsockname()[1]
        stop = threading.Event()

        def serve_health() -> None:
            while not stop.is_set():
                try:
                    connection, _ = occupied.accept()
                except (OSError, socket.timeout):
                    continue
                with connection:
                    connection.recv(4096)
                    connection.sendall(
                        b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"
                    )

        thread = threading.Thread(target=serve_health, daemon=True)
        thread.start()
        try:
            with urllib.request.urlopen(
                f"http://127.0.0.1:{port}/healthz",
                timeout=1,
            ) as response:
                self.assertEqual(response.status, 200)
            process_started = False

            def unexpected_process_start(*_, **__):
                nonlocal process_started
                process_started = True
                raise AssertionError("occupied port started a server")

            started = time.monotonic()
            with self.assertRaisesRegex(
                RuntimeError, "signaling address already in use"
            ):
                self.smoke.start_signaling_server(
                    Path("."),
                    "127.0.0.1",
                    port,
                    process_factory=unexpected_process_start,
                )
            self.assertLess(time.monotonic() - started, 1)
            self.assertFalse(process_started)
        finally:
            stop.set()
            occupied.close()
            thread.join(timeout=2)

    def test_server_exit_before_health_is_reported(self):
        process = subprocess.Popen(
            [sys.executable, "-c", "raise SystemExit(7)"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        try:
            with self.assertRaisesRegex(
                RuntimeError, "signaling service exited before healthy"
            ):
                self.smoke.wait_for_health(
                    "http://127.0.0.1:9/healthz",
                    process,
                    timeout_seconds=1,
                )
        finally:
            self.smoke.terminate_process_group(process, grace_seconds=0.1)

    def test_missing_room_line_times_out_and_process_group_is_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            child_pid_path = Path(directory) / "child.pid"
            child_code = (
                "import signal,time;"
                "signal.signal(signal.SIGTERM, signal.SIG_IGN);"
                "time.sleep(60)"
            )
            host_code = (
                "import pathlib,signal,subprocess,sys,time;"
                "child=subprocess.Popen([sys.executable,'-c',sys.argv[2]]);"
                "pathlib.Path(sys.argv[1]).write_text(str(child.pid));"
                "signal.signal(signal.SIGTERM, signal.SIG_IGN);"
                "time.sleep(60)"
            )
            host = subprocess.Popen(
                [
                    sys.executable,
                    "-c",
                    host_code,
                    str(child_pid_path),
                    child_code,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                start_new_session=True,
            )
            deadline = time.monotonic() + 2
            while not child_pid_path.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(child_pid_path.exists())
            child_pid = int(child_pid_path.read_text())
            started = time.monotonic()
            try:
                with self.assertRaisesRegex(RuntimeError, "host room timeout"):
                    self.smoke.wait_for_room(host, timeout_seconds=0.1)
            finally:
                self.smoke.terminate_process_group(host, grace_seconds=0.1)
            self.assertLess(time.monotonic() - started, 2)
            self.assertIsNotNone(host.poll())
            group_deadline = time.monotonic() + 2
            while time.monotonic() < group_deadline:
                try:
                    os.kill(child_pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.01)
            else:
                self.fail("host descendant survived process-group cleanup")

    def test_missing_movie_skew_is_rejected(self):
        output = (
            "RESULT connected=1 video=30 width=640 height=360 "
            "audio_sent=10 audio_received=10 audio_level=0.1 "
            "movie_audio_frames_received=0 sample_rate=0 channels=0 peak=0 "
            "chunks_generated=100 movie_av_skew_ms=-1 candidate=host error=\n"
        )
        with self.assertRaisesRegex(RuntimeError, "skew was unavailable"):
            self.smoke.validate("host", output, "synthetic", "movie", True)

    def test_already_exited_process_group_cleanup_is_idempotent(self):
        process = subprocess.Popen(
            [sys.executable, "-c", "pass"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        process.wait(timeout=2)
        self.smoke.terminate_process_group(process, grace_seconds=0.1)
        self.smoke.terminate_process_group(process, grace_seconds=0.1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", type=Path, required=True)
    args, unittest_args = parser.parse_known_args()
    SignaledCallSmokeTest.smoke = load_smoke_script(args.script)
    unittest.main(argv=[sys.argv[0], *unittest_args])
    return 0


if __name__ == "__main__":
    sys.exit(main())
