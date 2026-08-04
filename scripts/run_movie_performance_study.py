#!/usr/bin/env python3

"""Run and validate quality-preserving movie playback measurements.

The default path is deliberately conservative: it records sanitized, structured
counter lines and never overwrites an existing result.  Device-specific launch
commands are supplied by the caller so this script does not contain machine
paths, room identifiers, or credentials.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_signaled_call_smoke import (  # noqa: E402
    popen_group_options,
    start_managed_process,
    start_signaling_server,
    terminate_process_group,
    wait_for_health,
)


REQUIRED_RUNS = 3
SCENARIO_SECONDS = 180
ROOM_PATTERN = re.compile(r"^ROOM ([A-Z2-7]{6})$")
ALLOWED_KEYS = {
    "version", "role", "cpu_percent", "rss_bytes", "decoded", "offered",
    "encoded", "received", "callback", "submitted", "coalesced", "dropped",
    "conversion_failures", "width", "height", "cadence_num", "cadence_den",
    "pixel_aspect_num", "pixel_aspect_den", "color_range", "color_space",
    "codec", "profile", "path", "state", "candidate", "fallback_copies",
}
ENUMS = {
    "role": {"host", "viewer"},
    "color_range": {"limited", "full", "unknown"},
    "path": {"software", "hardware", "auto", "unknown"},
    "state": {"playing", "paused", "seeking", "stopped", "unknown"},
    "candidate": {"host", "srflx", "relay", "unknown"},
}
INTEGER_KEYS = {
    "version", "rss_bytes", "decoded", "offered", "encoded", "received",
    "callback", "submitted", "coalesced", "dropped", "conversion_failures",
    "fallback_copies",
    "width", "height", "cadence_num", "cadence_den", "pixel_aspect_num",
    "pixel_aspect_den",
}
FLOAT_KEYS = {"cpu_percent"}
SENSITIVE_WORDS = ("ROOM", "room", "TOKEN", "token", "SDP", "sdp", "ICE", "ice")


def validate_run_count(count: int) -> int:
    if count != REQUIRED_RUNS:
        raise ValueError(f"the study requires exactly {REQUIRED_RUNS} sequential runs")
    return count


def build_host_command(
    demo: Path, server_url: str, movie: Path, video_acceleration: str
) -> list[str]:
    return [
        str(demo), "--server", server_url, "--role", "host", "--source", "movie",
        "--movie", str(movie), "--movie-audio", "--video-acceleration",
        video_acceleration,
    ]


def build_viewer_command(demo: Path, server_url: str, room: str) -> list[str]:
    return [
        str(demo), "--server", server_url, "--role", "viewer", "--room", room,
        "--source", "test",
    ]


def prepare_output_root(root: Path, parent: Path) -> Path:
    parent = parent.resolve()
    root = root.resolve()
    try:
        root.relative_to(parent)
    except ValueError as error:
        raise ValueError("output root must be inside the declared parent") from error
    root.mkdir(parents=True, exist_ok=False)
    return root


def refuse_existing_artifact(path: Path) -> None:
    if path.exists():
        raise FileExistsError(f"refusing to overwrite existing artifact: {path.name}")


def _parse_value(key: str, value: str):
    if key in INTEGER_KEYS:
        parsed = int(value, 10)
        if parsed < 0:
            raise ValueError("counter values must be non-negative")
        return parsed
    if key in FLOAT_KEYS:
        parsed = float(value)
        if not math.isfinite(parsed) or parsed < 0:
            raise ValueError("CPU value must be finite and non-negative")
        return parsed
    if key in ENUMS:
        if value not in ENUMS[key]:
            raise ValueError(f"unsupported {key}")
        return value
    if key in {"color_space", "codec", "profile"}:
        if not re.fullmatch(r"[A-Za-z0-9_.+-]+", value):
            raise ValueError("metadata is not sanitized")
        return value
    raise ValueError(f"unsupported counter key: {key}")


def parse_perf_counters(line: str):
    fields = line.strip().split()
    if not fields or fields[0] != "PERF_COUNTERS":
        return None
    parsed = {}
    try:
        for field in fields[1:]:
            key, value = field.split("=", 1)
            if key in parsed or key not in ALLOWED_KEYS:
                return None
            parsed[key] = _parse_value(key, value)
    except (ValueError, TypeError):
        return None
    if parsed.get("version") != 1 or "role" not in parsed:
        return None
    if any(word in line for word in SENSITIVE_WORDS):
        return None
    return parsed


def redact_diagnostic(message: str, sensitive_values: Iterable[str] = ()) -> str:
    redacted = message
    for value in sensitive_values:
        if value:
            redacted = redacted.replace(value, "[redacted]")
    redacted = re.sub(r"/(?:[^\s/]+/)+[^\s]+", "[path-redacted]", redacted)
    redacted = re.sub(r"\b(?:ROOM|room|TOKEN|token|SDP|sdp|ICE|ice)\b[^\s]*", "[redacted]", redacted)
    return redacted


def is_complete_artifact(path: Path) -> bool:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
        if not lines:
            return False
        summary = json.loads(lines[-1])
        return summary.get("kind") == "summary" and summary.get("complete") is True
    except (OSError, ValueError, TypeError):
        return False


def synthetic_passing_report() -> dict:
    return {
        "exact_dimensions": True,
        "exact_metadata": True,
        "cadence_ratio": 1.0,
        "additional_drops": 0,
        "psnr_db": 45.0,
        "ssim": 0.995,
        "combined_average_cpu_reduction": 0.30,
        "candidate_cpu_p95_regression": 0.0,
        "rss_p95_growth": 0.10,
        "paused_cpu_reduction": 0.70,
        "one_frame_backlog_bound": True,
    }


def gates_pass(report: dict) -> bool:
    return (
        report.get("exact_dimensions") is True
        and report.get("exact_metadata") is True
        and report.get("cadence_ratio", 0) >= 0.99
        and report.get("additional_drops", 1) <= 0
        and report.get("psnr_db", 0) >= 45.0
        and report.get("ssim", 0) >= 0.995
        and report.get("combined_average_cpu_reduction", 0) >= 0.30
        and report.get("candidate_cpu_p95_regression", 1) <= 0
        and report.get("rss_p95_growth", 1) <= 0.10
        and report.get("paused_cpu_reduction", 0) >= 0.70
        and report.get("one_frame_backlog_bound") is True
    )


class PerformanceStudyError(RuntimeError):
    pass


class OutputReader:
    def __init__(self, process: subprocess.Popen[str]):
        self.process = process
        self.events: queue.Queue[str | None] = queue.Queue()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        stream = self.process.stdout
        if stream is None:
            self.events.put(None)
            return
        try:
            for line in stream:
                self.events.put(line)
        finally:
            self.events.put(None)


def _start_demo(command: list[str], environment: dict[str, str]):
    try:
        process = start_managed_process(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", env=environment,
            **popen_group_options(),
        )
    except OSError as error:
        raise PerformanceStudyError("process-start-failed") from error
    return process, OutputReader(process)


def _read_process_metrics(process: subprocess.Popen[str]) -> tuple[float, int]:
    if process.poll() is not None or sys.platform == "win32":
        return 0.0, 0
    try:
        result = subprocess.run(
            ["ps", "-o", "%cpu=", "-o", "rss=", "-p", str(process.pid)],
            check=False, capture_output=True, text=True, timeout=1,
        )
        fields = result.stdout.split()
        if len(fields) >= 2:
            return float(fields[0]), int(fields[1]) * 1024
    except (OSError, ValueError, subprocess.TimeoutExpired):
        pass
    return 0.0, 0


def _drain_counter_events(
    reader: OutputReader, role: str, elapsed_seconds: int, output
) -> int:
    count = 0
    while True:
        try:
            line = reader.events.get_nowait()
        except queue.Empty:
            return count
        if line is None:
            return count
        parsed = parse_perf_counters(line)
        if parsed is not None:
            _write_jsonl_line(output, {
                "kind": "counter", "elapsed_seconds": elapsed_seconds,
                "role": role, **parsed,
            })
            count += 1


def _wait_for_room(reader: OutputReader, process: subprocess.Popen[str]) -> str:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise PerformanceStudyError("host-exited-before-room")
        try:
            line = reader.events.get(timeout=0.25)
        except queue.Empty:
            continue
        if line is None:
            raise PerformanceStudyError("host-output-closed-before-room")
        match = ROOM_PATTERN.fullmatch(line.strip())
        if match:
            return match.group(1)
    raise PerformanceStudyError("room-timeout")


def run_real_session(
    demo: Path,
    server_url: str,
    movie: Path,
    output_path: Path,
    video_acceleration: str,
    duration_seconds: int = SCENARIO_SECONDS,
    environment: dict[str, str] | None = None,
) -> dict:
    refuse_existing_artifact(output_path)
    env = os.environ.copy()
    if environment:
        env.update(environment)
    env["SHAREME_PERFORMANCE_COUNTERS"] = "1"
    host = viewer = None
    host_reader = viewer_reader = None
    counter_count = 0
    failure = None
    started = time.monotonic()
    with output_path.open("x", encoding="utf-8") as output:
        _write_jsonl_line(output, {
            "kind": "run", "version": 1, "mode": video_acceleration,
            "duration_seconds": duration_seconds,
        })
        try:
            host, host_reader = _start_demo(
                build_host_command(demo, server_url, movie, video_acceleration), env
            )
            room = _wait_for_room(host_reader, host)
            viewer, viewer_reader = _start_demo(
                build_viewer_command(demo, server_url, room), env
            )
            deadline = started + duration_seconds
            next_metrics = started
            while time.monotonic() < deadline:
                if viewer.poll() is not None:
                    raise PerformanceStudyError("viewer-exited-during-measurement")
                if host.poll() is not None:
                    raise PerformanceStudyError("host-exited-during-measurement")
                now = time.monotonic()
                elapsed = int(now - started)
                counter_count += _drain_counter_events(
                    host_reader, "host", elapsed, output
                )
                counter_count += _drain_counter_events(
                    viewer_reader, "viewer", elapsed, output
                )
                if now >= next_metrics:
                    for process, role in ((host, "host"), (viewer, "viewer")):
                        cpu, rss = _read_process_metrics(process)
                        _write_jsonl_line(output, {
                            "kind": "process", "elapsed_seconds": elapsed,
                            "role": role, "cpu_percent": cpu, "rss_bytes": rss,
                        })
                    next_metrics += 1
                time.sleep(0.05)
            counter_count += _drain_counter_events(
                host_reader, "host", duration_seconds, output
            )
            counter_count += _drain_counter_events(
                viewer_reader, "viewer", duration_seconds, output
            )
        except PerformanceStudyError as error:
            failure = str(error)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            failure = redact_diagnostic(str(error))
        finally:
            for process in (viewer, host):
                if process is not None and process.poll() is None:
                    terminate_process_group(process, grace_seconds=1)
            _write_jsonl_line(output, {
                "kind": "summary", "complete": failure is None,
                "failure": failure, "counter_count": counter_count,
                "platform": sys.platform,
            })
    if failure is not None:
        raise PerformanceStudyError(f"{failure}; partial-artifact={output_path.name}")
    return {"artifact": output_path.name, "counter_count": counter_count}


def run_study(args: argparse.Namespace) -> dict:
    validate_run_count(args.run_count)
    if not args.demo.is_file():
        raise PerformanceStudyError("demo-not-found")
    if not args.movie.is_file():
        raise PerformanceStudyError("movie-not-found")
    root = prepare_output_root(args.output_root, args.output_parent)
    from urllib.parse import urlparse

    parsed = urlparse(args.server_url)
    if parsed.port is None or args.server_root is None:
        raise PerformanceStudyError("server-root-and-port-required")
    server = server_log = server_directory = None
    reports = []
    try:
        server, server_log, server_directory = start_signaling_server(
            args.server_root, parsed.hostname or "127.0.0.1", parsed.port
        )
        wait_for_health(
            f"http://{parsed.hostname or '127.0.0.1'}:{parsed.port}/healthz",
            server, server_log=server_log,
        )
        for index in range(1, REQUIRED_RUNS + 1):
            reports.append(run_real_session(
                args.demo, args.server_url, args.movie,
                root / f"run-{index:02d}.jsonl", args.video_acceleration,
                args.duration_seconds,
            ))
    finally:
        if server is not None and server.poll() is None:
            terminate_process_group(server, grace_seconds=1)
        if server_log is not None:
            server_log.close()
        if server_directory is not None:
            server_directory.cleanup()
    return {"runs": reports, "run_count": len(reports), "mode": args.video_acceleration}


def _write_jsonl_line(handle, value: dict) -> None:
    handle.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
    handle.flush()


def run_command(command: list[str], artifact: Path, duration: int = SCENARIO_SECONDS) -> int:
    """Capture only allowlisted counter lines from one sequential scenario."""
    refuse_existing_artifact(artifact)
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, bufsize=1)
    complete = False
    failure = None
    with artifact.open("x", encoding="utf-8") as output:
        _write_jsonl_line(output, {"kind": "run", "version": 1})
        try:
            assert process.stdout is not None
            for line in process.stdout:
                parsed = parse_perf_counters(line)
                if parsed is not None:
                    _write_jsonl_line(output, {"kind": "counter", **parsed})
                if time.monotonic() - started >= duration:
                    process.terminate()
                    break
            return_code = process.wait(timeout=10)
            if return_code == 0 or time.monotonic() - started >= duration:
                complete = True
            else:
                failure = f"process exited with status {return_code}"
        except (OSError, subprocess.TimeoutExpired) as error:
            failure = redact_diagnostic(str(error))
            process.kill()
            process.wait()
        finally:
            _write_jsonl_line(output, {
                "kind": "summary", "complete": complete,
                "failure": failure,
            })
    return 0 if complete else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--output-parent", type=Path, required=True)
    parser.add_argument("--run-count", type=int, default=REQUIRED_RUNS)
    parser.add_argument("--demo", type=Path)
    parser.add_argument("--server-url")
    parser.add_argument("--server-root", type=Path)
    parser.add_argument("--movie", type=Path)
    parser.add_argument("--video-acceleration", choices=("auto", "software"),
                        default="software")
    parser.add_argument("--duration-seconds", type=int, default=SCENARIO_SECONDS)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    validate_run_count(args.run_count)
    if args.demo is not None or args.movie is not None or args.server_url is not None:
        if args.demo is None or args.movie is None or args.server_url is None:
            parser.error("real study requires --demo, --movie, and --server-url")
        if args.duration_seconds != SCENARIO_SECONDS:
            parser.error("the frozen study duration is 180 seconds")
        try:
            print(json.dumps(run_study(args), sort_keys=True))
        except PerformanceStudyError as error:
            print(redact_diagnostic(str(error)), file=sys.stderr)
            return 1
        return 0
    root = prepare_output_root(args.output_root, args.output_parent)
    if not args.command:
        parser.error("a measurement command is required")
    for index in range(1, args.run_count + 1):
        result = run_command(args.command, root / f"run-{index:02d}.jsonl")
        if result:
            return result
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
