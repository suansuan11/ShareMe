#!/usr/bin/env python3

import argparse
import errno
import os
import queue
import re
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path
from typing import Optional, TextIO


RESULT = re.compile(
    r"RESULT connected=1 video=(\d+) width=(\d+) height=(\d+) "
    r"audio_sent=(\d+) audio_received=(\d+) audio_level=([0-9.eE+-]+) "
    r"movie_audio_frames_received=(\d+) "
    r"movie_audio_invalid_frames_received=(\d+) "
    r"sample_rate=(\d+) channels=(\d+) peak=(\d+) "
    r"chunks_generated=(\d+) movie_av_skew_ms=(-?\d+) "
    r"candidate=([^ ]+) error=$"
)


class SmokeRuntimeError(RuntimeError):
    pass


class SignalingStartupError(SmokeRuntimeError):
    def __init__(self, message: str, diagnostic: str = ""):
        super().__init__(message)
        self.diagnostic = diagnostic


def read_log_tail(log: TextIO, limit: int = 4096) -> str:
    log.flush()
    log.seek(0, os.SEEK_END)
    end = log.tell()
    log.seek(max(0, end - limit))
    return log.read()


def ensure_address_available(host: str, port: int) -> None:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind((host, port))
    except OSError as error:
        if error.errno == errno.EADDRINUSE:
            raise SignalingStartupError(
                "signaling address already in use"
            ) from error
        raise SignalingStartupError("signaling address unavailable") from error


def start_signaling_server(
    server_root: Path,
    host: str,
    port: int,
    process_factory=subprocess.Popen,
) -> tuple[subprocess.Popen[str], TextIO]:
    ensure_address_available(host, port)
    environment = os.environ.copy()
    environment["SHAREME_SIGNALING_ADDR"] = f"{host}:{port}"
    log = tempfile.TemporaryFile(mode="w+t", encoding="utf-8")
    try:
        process = process_factory(
            ["go", "run", "./cmd/signaling"],
            cwd=server_root,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
    except OSError as error:
        log.close()
        raise SignalingStartupError(
            "signaling service could not start"
        ) from error
    return process, log


def wait_for_health(
    url: str,
    server: subprocess.Popen[str],
    timeout_seconds: float = 10,
    server_log: Optional[TextIO] = None,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if server.poll() is not None:
            diagnostic = read_log_tail(server_log) if server_log else ""
            raise SignalingStartupError(
                "signaling service exited before healthy", diagnostic
            )
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                if response.status == 200:
                    time.sleep(0.05)
                    if server.poll() is not None:
                        diagnostic = (
                            read_log_tail(server_log) if server_log else ""
                        )
                        raise SignalingStartupError(
                            "signaling service exited before healthy",
                            diagnostic,
                        )
                    return
        except OSError:
            if server.poll() is not None:
                diagnostic = read_log_tail(server_log) if server_log else ""
                raise SignalingStartupError(
                    "signaling service exited before healthy", diagnostic
                )
            time.sleep(0.1)
    diagnostic = read_log_tail(server_log) if server_log else ""
    raise SignalingStartupError(
        "signaling service did not become healthy", diagnostic
    )


def validate(
    label: str,
    output: str,
    audio_mode: str,
    video_mode: str,
    movie_audio: bool,
) -> None:
    lines = [line for line in output.splitlines() if line.startswith("RESULT ")]
    match = RESULT.fullmatch(lines[-1]) if lines else None
    if match is None:
        raise SmokeRuntimeError(f"{label} did not report a valid result")
    video_frames, width, height, audio_sent, audio_received = (
        int(value) for value in match.groups()[:5]
    )
    if any(
        value <= 0
        for value in (video_frames, width, height, audio_sent, audio_received)
    ):
        raise SmokeRuntimeError(
            f"{label} did not report received test media"
        )
    if audio_mode == "microphone" and float(match.group(6)) <= 0.0:
        raise SmokeRuntimeError(
            f"{label} did not report local microphone activity"
        )
    if video_mode == "movie" and label == "viewer":
        if video_frames < 20 or (width, height) != (320, 180):
            raise SmokeRuntimeError(
                "viewer did not receive the expected movie video"
            )
    if movie_audio and label == "viewer":
        movie_frames, invalid_frames, sample_rate, channels, peak = (
            int(value) for value in match.groups()[6:11]
        )
        if movie_frames < 100:
            raise SmokeRuntimeError(
                "viewer did not receive enough movie audio"
            )
        if invalid_frames != 0:
            raise SmokeRuntimeError(
                "viewer received an invalid callback for movie audio"
            )
        if (sample_rate, channels) != (48_000, 2):
            raise SmokeRuntimeError(
                "viewer movie audio format was not 48 kHz stereo"
            )
        if peak <= 0:
            raise SmokeRuntimeError("viewer movie audio was silent")
    if movie_audio and label == "host":
        chunks_generated = int(match.group(12))
        movie_av_skew_ms = int(match.group(13))
        if chunks_generated < 100:
            raise SmokeRuntimeError(
                "host did not generate enough movie audio"
            )
        if movie_av_skew_ms < 0:
            raise SmokeRuntimeError(
                "host movie audio/video skew was unavailable"
            )
        if abs(movie_av_skew_ms) > 50:
            raise SmokeRuntimeError(
                "host movie audio/video skew exceeded 50 ms"
            )


def wait_for_room(
    host: subprocess.Popen[str], timeout_seconds: float = 10
) -> str:
    if host.stdout is None:
        raise SmokeRuntimeError("host room output unavailable")
    result: queue.Queue[str] = queue.Queue(maxsize=1)

    def read_line() -> None:
        try:
            line = host.stdout.readline()
        except (OSError, ValueError):
            line = ""
        result.put(line)

    reader = threading.Thread(target=read_line, daemon=True)
    reader.start()
    try:
        room_line = result.get(timeout=timeout_seconds).strip()
    except queue.Empty as error:
        raise SmokeRuntimeError("host room timeout") from error
    if not re.fullmatch(r"ROOM [A-Z2-7]{6}", room_line):
        raise SmokeRuntimeError("host did not create a valid room")
    return room_line


def process_group_exists(group_id: int) -> bool:
    try:
        os.killpg(group_id, 0)
        return True
    except (PermissionError, ProcessLookupError):
        return False


def terminate_process_group(
    process: subprocess.Popen[str], grace_seconds: float = 5
) -> None:
    group_id = process.pid
    if group_id <= 0 or group_id == os.getpgrp():
        raise SmokeRuntimeError("unsafe process group cleanup refused")
    try:
        os.killpg(group_id, signal.SIGTERM)
    except ProcessLookupError:
        pass
    except PermissionError:
        if process.poll() is None:
            process.terminate()
    deadline = time.monotonic() + grace_seconds
    while process_group_exists(group_id) and time.monotonic() < deadline:
        time.sleep(0.01)
    if process_group_exists(group_id):
        try:
            os.killpg(group_id, signal.SIGKILL)
        except (PermissionError, ProcessLookupError):
            pass
    if process.poll() is None:
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=grace_seconds)
    if process.stdout is not None:
        process.stdout.close()
    if process.stderr is not None:
        process.stderr.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--server-root", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument(
        "--audio",
        choices=("synthetic", "microphone"),
        default="synthetic",
    )
    parser.add_argument(
        "--video",
        choices=("synthetic", "movie"),
        default="synthetic",
    )
    parser.add_argument("--movie", type=Path)
    parser.add_argument("--movie-audio", action="store_true")
    args = parser.parse_args()
    if (args.video == "movie") != (args.movie is not None):
        parser.error("--video movie requires --movie, and --movie requires movie mode")
    if args.movie_audio and args.video != "movie":
        parser.error("--movie-audio requires --video movie and --movie")
    address = f"127.0.0.1:{args.port}"
    websocket_url = f"ws://{address}/v1/ws"
    server, server_log = start_signaling_server(
        args.server_root, "127.0.0.1", args.port
    )
    host = None
    viewer = None
    try:
        wait_for_health(
            f"http://{address}/healthz", server, server_log=server_log
        )
        host_command = [
            str(args.probe),
            "--server",
            websocket_url,
            "--role",
            "host",
            "--audio",
            args.audio,
            "--video",
            args.video,
        ]
        if args.movie is not None:
            host_command.extend(("--movie", str(args.movie)))
        if args.movie_audio:
            host_command.append("--movie-audio")
        try:
            host = subprocess.Popen(
                host_command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                start_new_session=True,
            )
        except OSError as error:
            raise SmokeRuntimeError("host process could not start") from error
        room_line = wait_for_room(host)
        room_id = room_line.split()[1]
        try:
            viewer = subprocess.Popen(
                [
                    str(args.probe),
                    "--server",
                    websocket_url,
                    "--role",
                    "viewer",
                    "--room",
                    room_id,
                    "--audio",
                    args.audio,
                    "--video",
                    "synthetic",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                start_new_session=True,
            )
        except OSError as error:
            raise SmokeRuntimeError(
                "viewer process could not start"
            ) from error
        viewer_output, viewer_error = viewer.communicate(timeout=25)
        if viewer.returncode != 0:
            raise SmokeRuntimeError(
                f"viewer failed: {viewer_error.strip()}"
            )
        host_output, host_error = host.communicate(timeout=25)
        if host.returncode != 0:
            raise SmokeRuntimeError(f"host failed: {host_error.strip()}")
        validate("viewer", viewer_output, args.audio, args.video, args.movie_audio)
        validate("host", host_output, args.audio, args.video, args.movie_audio)
        print(room_line)
        print(viewer_output.strip())
        print(host_output.strip())
        return 0
    finally:
        if viewer is not None:
            terminate_process_group(viewer)
        if host is not None:
            terminate_process_group(host)
        terminate_process_group(server)
        server_log.close()


def cli_main() -> int:
    try:
        return main()
    except SignalingStartupError:
        print("SMOKE_ERROR signaling-startup-failed", file=sys.stderr)
        return 1
    except (SmokeRuntimeError, subprocess.TimeoutExpired, TimeoutError):
        print("SMOKE_ERROR smoke-failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(cli_main())
