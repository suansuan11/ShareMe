#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path


def run(probe: Path, arguments: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(probe), *arguments],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--has-movie-rtc", action="store_true")
    args = parser.parse_args()

    server = ["--server", "ws://127.0.0.1:1/v1/ws"]
    common = [*server, "--audio", "synthetic"]
    secret_path = "/private/secret-movie.mp4"
    invalid_cases = (
        [
            *common,
            "--role",
            "viewer",
            "--room",
            "ABCDEF",
            "--video",
            "movie",
            "--movie",
            secret_path,
            "--movie-audio",
        ],
        [
            *common,
            "--role",
            "host",
            "--video",
            "movie",
            "--movie-audio",
        ],
        [
            *common,
            "--role",
            "host",
            "--video",
            "synthetic",
            "--movie-audio",
        ],
    )
    for arguments in invalid_cases:
        result = run(args.probe, arguments)
        if (result.returncode, result.stdout, result.stderr) != (2, "", ""):
            raise RuntimeError("invalid movie-audio CLI combination was accepted")

    valid = [
        *common,
        "--role",
        "host",
        "--video",
        "movie",
        "--movie",
        secret_path,
        "--movie-audio",
    ]
    result = run(args.probe, valid)
    combined = result.stdout + result.stderr
    if secret_path in combined:
        raise RuntimeError("movie path leaked from CLI output")
    if args.has_movie_rtc:
        if result.returncode == 2:
            raise RuntimeError("movie-call CLI rejected a valid argument combination")
        if "PEER_ERROR movie-audio-dependency-unavailable" in combined:
            raise RuntimeError("movie-call CLI reported a missing compiled dependency")
    elif (
        result.returncode,
        result.stdout,
        result.stderr,
    ) != (1, "", "PEER_ERROR movie-audio-dependency-unavailable\n"):
        raise RuntimeError("call-only dependency error contract changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
