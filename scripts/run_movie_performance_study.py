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
import re
import subprocess
import time
from pathlib import Path
from typing import Iterable


REQUIRED_RUNS = 3
SCENARIO_SECONDS = 180
ALLOWED_KEYS = {
    "version", "role", "cpu_percent", "rss_bytes", "decoded", "offered",
    "encoded", "received", "callback", "submitted", "coalesced", "dropped",
    "conversion_failures", "width", "height", "cadence_num", "cadence_den",
    "pixel_aspect_num", "pixel_aspect_den", "color_range", "color_space",
    "codec", "profile", "path", "state", "candidate",
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
    "width", "height", "cadence_num", "cadence_den", "pixel_aspect_num",
    "pixel_aspect_den",
}
FLOAT_KEYS = {"cpu_percent"}
SENSITIVE_WORDS = ("ROOM", "room", "TOKEN", "token", "SDP", "sdp", "ICE", "ice")


def validate_run_count(count: int) -> int:
    if count != REQUIRED_RUNS:
        raise ValueError(f"the study requires exactly {REQUIRED_RUNS} sequential runs")
    return count


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
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    validate_run_count(args.run_count)
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
