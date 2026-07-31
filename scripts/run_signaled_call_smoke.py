#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys
import time
import urllib.request
from pathlib import Path


RESULT = re.compile(
    r"RESULT connected=1 video=(\d+) width=(\d+) height=(\d+) "
    r"audio_sent=(\d+) audio_received=(\d+) audio_level=([0-9.eE+-]+) "
    r"movie_audio_frames_received=(\d+) sample_rate=(\d+) channels=(\d+) "
    r"peak=(\d+) chunks_generated=(\d+) movie_av_skew_ms=(-?\d+) "
    r"candidate=([^ ]+) error=$"
)


def wait_for_health(url: str) -> None:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                if response.status == 200:
                    return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("signaling service did not become healthy")


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
        raise RuntimeError(f"{label} did not report a valid result")
    video_frames, width, height, audio_sent, audio_received = (
        int(value) for value in match.groups()[:5]
    )
    if any(
        value <= 0
        for value in (video_frames, width, height, audio_sent, audio_received)
    ):
        raise RuntimeError(f"{label} did not report received test media")
    if audio_mode == "microphone" and float(match.group(6)) <= 0.0:
        raise RuntimeError(f"{label} did not report local microphone activity")
    if video_mode == "movie" and label == "viewer":
        if video_frames < 20 or (width, height) != (320, 180):
            raise RuntimeError("viewer did not receive the expected movie video")
    if movie_audio and label == "viewer":
        movie_frames, sample_rate, channels, peak = (
            int(value) for value in match.groups()[6:10]
        )
        if movie_frames < 100:
            raise RuntimeError("viewer did not receive enough movie audio")
        if (sample_rate, channels) != (48_000, 2):
            raise RuntimeError("viewer movie audio format was not 48 kHz stereo")
        if peak <= 0:
            raise RuntimeError("viewer movie audio was silent")
    if movie_audio and label == "host":
        chunks_generated = int(match.group(11))
        movie_av_skew_ms = int(match.group(12))
        if chunks_generated < 100:
            raise RuntimeError("host did not generate enough movie audio")
        if abs(movie_av_skew_ms) > 50:
            raise RuntimeError("host movie audio/video skew exceeded 50 ms")


def terminate(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


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
    environment = os.environ.copy()
    environment["SHAREME_SIGNALING_ADDR"] = address
    server = subprocess.Popen(
        ["go", "run", "./cmd/signaling"],
        cwd=args.server_root,
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    host = None
    try:
        wait_for_health(f"http://{address}/healthz")
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
        host = subprocess.Popen(
            host_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        room_line = host.stdout.readline().strip()
        if not re.fullmatch(r"ROOM [A-Z2-7]{6}", room_line):
            raise RuntimeError("host did not create a valid room")
        room_id = room_line.split()[1]
        viewer = subprocess.run(
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
            capture_output=True,
            text=True,
            timeout=25,
            check=True,
        )
        host_output, host_error = host.communicate(timeout=25)
        if host.returncode != 0:
            raise RuntimeError(f"host failed: {host_error.strip()}")
        validate("viewer", viewer.stdout, args.audio, args.video, args.movie_audio)
        validate("host", host_output, args.audio, args.video, args.movie_audio)
        print(room_line)
        print(viewer.stdout.strip())
        print(host_output.strip())
        return 0
    finally:
        if host is not None:
            terminate(host)
        terminate(server)


if __name__ == "__main__":
    sys.exit(main())
