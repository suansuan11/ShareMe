#!/usr/bin/env python3

import argparse
import ctypes
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
    r"chunks_generated=(\d+) "
    r"candidate=([^ ]+) error=$"
)


class SmokeRuntimeError(RuntimeError):
    pass


class SignalingStartupError(SmokeRuntimeError):
    def __init__(self, message: str, diagnostic: str = ""):
        super().__init__(message)
        self.diagnostic = diagnostic


def popen_group_options() -> dict[str, object]:
    if os.name == "nt":
        return {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
    return {"start_new_session": True}


def read_log_tail(log: TextIO, limit: int = 4096) -> str:
    log.flush()
    log.seek(0, os.SEEK_END)
    end = log.tell()
    log.seek(max(0, end - limit))
    return log.read()


def cleanup_temporary_directory(
    directory,
    *,
    attempts: int = 20,
    retry_delay_seconds: float = 0.05,
) -> None:
    if attempts <= 0:
        raise ValueError("cleanup attempts must be positive")
    for attempt in range(attempts):
        try:
            directory.cleanup()
            return
        except PermissionError:
            if attempt + 1 == attempts:
                raise
            time.sleep(retry_delay_seconds)


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
) -> tuple[
    subprocess.Popen[str], TextIO, tempfile.TemporaryDirectory[str]
]:
    ensure_address_available(host, port)
    environment = os.environ.copy()
    environment["SHAREME_SIGNALING_ADDR"] = f"{host}:{port}"
    log = tempfile.TemporaryFile(mode="w+t", encoding="utf-8")
    binary_directory = tempfile.TemporaryDirectory(
        prefix="shareme-signaling-"
    )
    executable = Path(binary_directory.name) / (
        "signaling.exe" if os.name == "nt" else "signaling"
    )
    try:
        build = subprocess.run(
            [
                "go",
                "build",
                "-o",
                str(executable),
                "./cmd/signaling",
            ],
            cwd=server_root,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if build.returncode != 0:
            diagnostic = read_log_tail(log)
            log.close()
            cleanup_temporary_directory(binary_directory)
            raise SignalingStartupError(
                "signaling service could not build", diagnostic
            )
        process = start_managed_process(
            [str(executable)],
            process_factory=process_factory,
            cwd=server_root,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            **popen_group_options(),
        )
    except OSError as error:
        log.close()
        cleanup_temporary_directory(binary_directory)
        raise SignalingStartupError(
            "signaling service could not start"
        ) from error
    return process, log, binary_directory


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
        if video_frames < 20 or not valid_movie_dimensions(width, height):
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
        if chunks_generated < 100:
            raise SmokeRuntimeError(
                "host did not generate enough movie audio"
            )


def valid_movie_dimensions(width: int, height: int) -> bool:
    return (
        width > 0
        and height > 0
        and width % 2 == 0
        and height % 2 == 0
        and width * 9 == height * 16
    )


def wait_for_room(
    host: subprocess.Popen[str], timeout_seconds: float = 10
) -> str:
    if host.stdout is None:
        raise SmokeRuntimeError("host room output unavailable")
    deadline = time.monotonic() + timeout_seconds
    observed_lines: list[str] = []
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise SmokeRuntimeError("host room timeout")
        result: queue.Queue[str] = queue.Queue(maxsize=1)
        reader_errors: list[str] = []

        def read_line() -> None:
            try:
                result.put(host.stdout.readline())
            except (OSError, UnicodeError, ValueError) as error:
                reader_errors.append(f"{type(error).__name__}: {error}")
                result.put("")

        reader = threading.Thread(target=read_line, daemon=True)
        reader.start()
        try:
            line = result.get(timeout=remaining)
        except queue.Empty as error:
            raise SmokeRuntimeError("host room timeout") from error
        room_line = line.strip()
        if re.fullmatch(r"ROOM [A-Z2-7]{6}", room_line):
            return room_line
        if line == "":
            try:
                host.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                pass
            output = " | ".join(observed_lines[-5:])
            reader_error = " | ".join(reader_errors)
            context = (
                output
                or reader_error
                or f"exit={host.poll()}"
            )
            detail = f": {context}" if context else ""
            raise SmokeRuntimeError(
                f"host did not create a valid room{detail}"
            )
        if room_line:
            observed_lines.append(room_line)


def process_group_exists(group_id: int) -> bool:
    try:
        os.killpg(group_id, 0)
        return True
    except (PermissionError, ProcessLookupError):
        return False


def attach_windows_kill_job(process: subprocess.Popen[str]) -> None:
    if os.name != "nt":
        return

    class BasicLimitInformation(ctypes.Structure):
        _fields_ = [
            ("PerProcessUserTimeLimit", ctypes.c_longlong),
            ("PerJobUserTimeLimit", ctypes.c_longlong),
            ("LimitFlags", ctypes.c_ulong),
            ("MinimumWorkingSetSize", ctypes.c_size_t),
            ("MaximumWorkingSetSize", ctypes.c_size_t),
            ("ActiveProcessLimit", ctypes.c_ulong),
            ("Affinity", ctypes.c_size_t),
            ("PriorityClass", ctypes.c_ulong),
            ("SchedulingClass", ctypes.c_ulong),
        ]

    class IoCounters(ctypes.Structure):
        _fields_ = [
            ("ReadOperationCount", ctypes.c_ulonglong),
            ("WriteOperationCount", ctypes.c_ulonglong),
            ("OtherOperationCount", ctypes.c_ulonglong),
            ("ReadTransferCount", ctypes.c_ulonglong),
            ("WriteTransferCount", ctypes.c_ulonglong),
            ("OtherTransferCount", ctypes.c_ulonglong),
        ]

    class ExtendedLimitInformation(ctypes.Structure):
        _fields_ = [
            ("BasicLimitInformation", BasicLimitInformation),
            ("IoInfo", IoCounters),
            ("ProcessMemoryLimit", ctypes.c_size_t),
            ("JobMemoryLimit", ctypes.c_size_t),
            ("PeakProcessMemoryUsed", ctypes.c_size_t),
            ("PeakJobMemoryUsed", ctypes.c_size_t),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
    kernel32.CreateJobObjectW.restype = ctypes.c_void_p
    kernel32.SetInformationJobObject.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_ulong,
    ]
    kernel32.SetInformationJobObject.restype = ctypes.c_int
    kernel32.AssignProcessToJobObject.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    kernel32.AssignProcessToJobObject.restype = ctypes.c_int
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.restype = ctypes.c_int

    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    information = ExtendedLimitInformation()
    information.BasicLimitInformation.LimitFlags = 0x00002000
    configured = kernel32.SetInformationJobObject(
        job, 9, ctypes.byref(information), ctypes.sizeof(information)
    )
    assigned = configured and kernel32.AssignProcessToJobObject(
        job, int(process._handle)
    )
    if not assigned:
        error = ctypes.get_last_error()
        kernel32.CloseHandle(job)
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        raise ctypes.WinError(error)
    setattr(process, "_shareme_job_handle", job)


def close_windows_kill_job(process: subprocess.Popen[str]) -> None:
    job = getattr(process, "_shareme_job_handle", None)
    if not job:
        return
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.restype = ctypes.c_int
    kernel32.CloseHandle(job)
    setattr(process, "_shareme_job_handle", None)


def start_managed_process(
    command: list[str],
    process_factory=subprocess.Popen,
    **options,
) -> subprocess.Popen[str]:
    if os.name != "nt":
        return process_factory(command, **options)
    if "stdin" in options:
        raise ValueError("managed process stdin is reserved")
    wrapper = (
        "import subprocess,sys;"
        "sys.stdin.buffer.read(1);"
        "child=subprocess.Popen(sys.argv[1:]);"
        "raise SystemExit(child.wait())"
    )
    process = process_factory(
        [sys.executable, "-c", wrapper, *command],
        stdin=subprocess.PIPE,
        **options,
    )
    try:
        attach_windows_kill_job(process)
        if process.stdin is None:
            raise OSError("managed process gate unavailable")
        gate = "1" if options.get("text") or options.get("encoding") else b"1"
        process.stdin.write(gate)
        process.stdin.close()
        process.stdin = None
    except BaseException:
        terminate_process_group(process, grace_seconds=0.1)
        raise
    return process


def terminate_process_group(
    process: subprocess.Popen[str], grace_seconds: float = 5
) -> None:
    if os.name == "nt":
        if process.poll() is None:
            try:
                process.send_signal(signal.CTRL_BREAK_EVENT)
                process.wait(timeout=grace_seconds)
            except (OSError, subprocess.TimeoutExpired):
                pass
        close_windows_kill_job(process)
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
        return

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
        choices=("synthetic", "movie", "desktop"),
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
    server, server_log, server_binary_directory = start_signaling_server(
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
            host = start_managed_process(
                host_command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                **popen_group_options(),
            )
        except OSError as error:
            raise SmokeRuntimeError("host process could not start") from error
        room_line = wait_for_room(host)
        room_id = room_line.split()[1]
        try:
            viewer = start_managed_process(
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
                encoding="utf-8",
                errors="replace",
                **popen_group_options(),
            )
        except OSError as error:
            raise SmokeRuntimeError(
                "viewer process could not start"
            ) from error
        viewer_output, viewer_error = viewer.communicate(timeout=25)
        if "codec collision" in viewer_error or "RaceDetected" in viewer_error:
            raise SmokeRuntimeError("viewer runtime failure")
        if viewer.returncode != 0:
            raise SmokeRuntimeError(
                f"viewer failed: {viewer_error.strip()}"
            )
        host_output, host_error = host.communicate(timeout=25)
        if "codec collision" in host_error or "RaceDetected" in host_error:
            raise SmokeRuntimeError("host runtime failure")
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
        cleanup_temporary_directory(server_binary_directory)


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
