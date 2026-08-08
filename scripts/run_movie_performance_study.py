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
import statistics
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
MEASUREMENT_START_SECONDS = 30
MEASUREMENT_END_SECONDS = 150
MEASUREMENT_SAMPLE_SECONDS = MEASUREMENT_END_SECONDS - MEASUREMENT_START_SECONDS
ROOM_PATTERN = re.compile(r"^ROOM ([A-Z2-7]{6})$")
ALLOWED_KEYS = {
    "version", "role", "cpu_percent", "rss_bytes", "decoded", "offered",
    "encoded", "received", "callback", "submitted", "coalesced", "dropped",
    "conversion_failures", "width", "height", "cadence_num", "cadence_den",
    "pixel_aspect_num", "pixel_aspect_den", "color_range", "color_space",
    "codec", "profile", "requested_mode", "decoder_path", "webrtc_encoder",
    "encoder_implementation",
    "bytes_sent", "bytes_received", "bitrate_bps",
    "hardware_encoder_status", "state", "candidate", "fallback_copies",
    "max_pending", "source_pending", "source_pending_bytes",
    "source_peak_pending", "source_peak_pending_bytes",
    "session_video_pending", "session_video_bytes", "session_audio_pending",
    "session_audio_bytes", "render_queue", "pending_callbacks",
    "pending_callback_bytes", "owned_bytes", "owned_peak_bytes",
    "backpressure_events", "stats_unavailable",
}
ENUMS = {
    "role": {"host", "viewer"},
    "color_range": {"limited", "full", "unknown"},
    "requested_mode": {"software", "auto"},
    "decoder_path": {"software", "hardware", "fallback"},
    "webrtc_encoder": {"vp8-software", "H264", "remote-unreported"},
    "encoder_implementation": {"vp8-software", "VP8Template", "VideoToolbox", "receive-only"},
    "hardware_encoder_status": {"unavailable-locked-abi", "active", "receive-only"},
    "state": {"playing", "paused", "seeking", "stopped", "unknown"},
    "candidate": {"host", "srflx", "relay", "unknown"},
}
CAPABILITY_KEYS = frozenset({
    "requested_mode", "decoder_path", "webrtc_encoder",
    "hardware_encoder_status",
})
INTEGER_KEYS = {
    "version", "rss_bytes", "decoded", "offered", "encoded", "received",
    "callback", "submitted", "coalesced", "dropped", "conversion_failures",
    "fallback_copies", "max_pending", "source_pending", "source_pending_bytes",
    "source_peak_pending", "source_peak_pending_bytes", "session_video_pending",
    "session_video_bytes", "session_audio_pending", "session_audio_bytes",
    "render_queue", "pending_callbacks", "pending_callback_bytes", "owned_bytes",
    "owned_peak_bytes", "backpressure_events", "stats_unavailable",
    "width", "height", "cadence_num", "cadence_den", "pixel_aspect_num",
    "pixel_aspect_den", "bytes_sent", "bytes_received", "bitrate_bps",
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


def _valid_capability_fields(fields: dict) -> bool:
    return all(
        key in fields
        and isinstance(fields[key], str)
        and fields[key] in ENUMS[key]
        for key in CAPABILITY_KEYS
    )


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
    if (
        parsed.get("version") != 1
        or "role" not in parsed
        or not _valid_capability_fields(parsed)
    ):
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


def _nearest_rank(values: list[float], percentile: int) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    rank = max(1, (len(ordered) * percentile + 99) // 100)
    return ordered[min(rank, len(ordered)) - 1]


def _artifact_records(path: Path) -> list[dict]:
    try:
        return [
            json.loads(line)
            for line in path.read_text(encoding="utf-8").splitlines()
        ]
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("invalid-performance-artifact") from error


def _artifact_demo_identity(path: Path) -> str:
    records = _artifact_records(path)
    runs = [record for record in records if record.get("kind") == "run"]
    identity = runs[-1].get("demo_sha256") if runs else None
    if not isinstance(identity, str) or not re.fullmatch(r"[0-9a-f]{64}", identity):
        raise ValueError("missing-demo-identity")
    return identity


def _measurement_process_samples(
    records: list[dict], role: str
) -> list[dict]:
    samples = [
        record for record in records
        if record.get("kind") == "process"
        and record.get("role") == role
        and record.get("phase") == "measurement"
    ]
    expected_elapsed = list(range(MEASUREMENT_START_SECONDS, MEASUREMENT_END_SECONDS))
    if [record.get("elapsed_seconds") for record in samples] != expected_elapsed:
        raise ValueError(f"measurement samples missing or non-contiguous: {role}")
    for sample in samples:
        cpu = sample.get("cpu_percent")
        rss = sample.get("rss_bytes")
        if (
            not isinstance(cpu, (int, float))
            or not math.isfinite(cpu)
            or cpu < 0
            or not isinstance(rss, int)
            or rss <= 0
        ):
            raise ValueError(f"invalid measurement samples: {role}")
    return samples


def _measurement_counter_samples(
    records: list[dict], role: str
) -> list[dict]:
    samples = [
        record for record in records
        if record.get("kind") == "counter"
        and record.get("role") == role
        and record.get("phase") == "measurement"
    ]
    expected_elapsed = list(range(MEASUREMENT_START_SECONDS, MEASUREMENT_END_SECONDS))
    if [record.get("elapsed_seconds") for record in samples] != expected_elapsed:
        raise ValueError(
            f"measurement counter samples missing or non-contiguous: {role}"
        )
    for record in records:
        if record.get("kind") == "counter" and not _valid_capability_fields(record):
            raise ValueError(f"invalid capability contract: {role}")
    if "max_pending" not in samples[-1]:
        raise ValueError(f"backlog depth missing: {role}")
    return samples


def summarize_performance_artifact(path: Path) -> dict:
    records = _artifact_records(path)
    summaries = [record for record in records if record.get("kind") == "summary"]
    if (
        not summaries
        or summaries[-1].get("complete") is not True
        or summaries[-1].get("failure") is not None
    ):
        raise ValueError("incomplete-performance-artifact")
    demo_sha256 = _artifact_demo_identity(path)
    result = {
        "artifact": path.name,
        "sha256": _sha256_file(path),
        "demo_sha256": demo_sha256,
        "complete": True,
        "failure": summaries[-1].get("failure"),
        "platform": summaries[-1].get("platform", "unknown"),
        "counterCount": int(summaries[-1].get("counter_count", 0)),
        "roles": {},
    }
    for role in ("host", "viewer"):
        processes = _measurement_process_samples(records, role)
        counters = _measurement_counter_samples(records, role)
        cpu = [float(record["cpu_percent"]) for record in processes]
        rss = [float(record["rss_bytes"]) for record in processes]
        last = counters[-1]
        result["roles"][role] = {
            "counterSamples": len(counters),
            "processSamples": len(processes),
            "decoded": int(last.get("decoded", 0)),
            "received": int(last.get("received", 0)),
            "submitted": int(last.get("submitted", 0)),
            "coalesced": int(last.get("coalesced", 0)),
            "dropped": int(last.get("dropped", 0)),
            "conversionFailures": int(last.get("conversion_failures", 0)),
            "fallbackCopies": int(last.get("fallback_copies", 0)),
            "width": int(last.get("width", 0)),
            "height": int(last.get("height", 0)),
            "cadenceNum": int(last.get("cadence_num", 0)),
            "cadenceDen": int(last.get("cadence_den", 0)),
            "pixelAspectNum": int(last.get("pixel_aspect_num", 0)),
            "pixelAspectDen": int(last.get("pixel_aspect_den", 0)),
            "colorRange": last.get("color_range", "unknown"),
            "colorSpace": last.get("color_space", "unknown"),
            "codec": last.get("codec", "unknown"),
            "profile": last.get("profile", "unknown"),
            "requestedMode": last["requested_mode"],
            "decoderPath": last["decoder_path"],
            "webrtcEncoder": last["webrtc_encoder"],
            "hardwareEncoderStatus": last["hardware_encoder_status"],
            "maxPending": int(last["max_pending"]),
            "cpuAverage": sum(cpu) / len(cpu) if cpu else 0.0,
            "cpuP95": _nearest_rank(cpu, 95),
            "rssP95": _nearest_rank(rss, 95),
        }
    result["combinedAverageCpu"] = sum(
        result["roles"][role]["cpuAverage"] for role in ("host", "viewer")
    )
    result["combinedCpuP95"] = sum(
        result["roles"][role]["cpuP95"] for role in ("host", "viewer")
    )
    result["combinedRssP95"] = sum(
        result["roles"][role]["rssP95"] for role in ("host", "viewer")
    )
    return result


def _capability_contract_summary(runs: list[dict]) -> dict:
    role_summaries = [
        run["roles"][role]
        for run in runs
        for role in ("host", "viewer")
    ]
    webrtc_encoders = {summary["webrtcEncoder"] for summary in role_summaries}
    hardware_statuses = {
        summary["hardwareEncoderStatus"] for summary in role_summaries
    }
    return {
        "requested_modes": sorted(
            {summary["requestedMode"] for summary in role_summaries}
        ),
        "decoder_paths": sorted(
            {summary["decoderPath"] for summary in role_summaries}
        ),
        "webrtc_encoder": (
            next(iter(webrtc_encoders)) if len(webrtc_encoders) == 1 else None
        ),
        "hardware_encoder_status": (
            next(iter(hardware_statuses)) if len(hardware_statuses) == 1 else None
        ),
    }


def _sha256_file(path: Path) -> str:
    import hashlib
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def aggregate_performance_runs(
    baseline_paths: list[Path], candidate_paths: list[Path]
) -> dict:
    baseline_identities = {_artifact_demo_identity(path) for path in baseline_paths}
    candidate_identities = {_artifact_demo_identity(path) for path in candidate_paths}
    if baseline_identities & candidate_identities:
        raise ValueError("baseline and candidate demo identities match")
    baseline = [summarize_performance_artifact(path) for path in baseline_paths]
    candidate = [summarize_performance_artifact(path) for path in candidate_paths]
    if len(baseline) != REQUIRED_RUNS or len(candidate) != REQUIRED_RUNS:
        raise ValueError("performance comparison requires three runs per side")
    baseline_median_cpu = statistics.median(
        run["combinedAverageCpu"] for run in baseline
    )
    candidate_median_cpu = statistics.median(
        run["combinedAverageCpu"] for run in candidate
    )
    baseline_median_p95 = statistics.median(
        run["combinedCpuP95"] for run in baseline
    )
    candidate_median_p95 = statistics.median(
        run["combinedCpuP95"] for run in candidate
    )
    baseline_median_rss = statistics.median(
        run["combinedRssP95"] for run in baseline
    )
    candidate_median_rss = statistics.median(
        run["combinedRssP95"] for run in candidate
    )
    run_role_comparisons = []
    cadence_ratios = []
    additional_drops = []
    backlog_bounds = []
    exact_dimensions = []
    exact_metadata = []
    for index, (baseline_run, candidate_run) in enumerate(
        zip(baseline, candidate), start=1
    ):
        role_comparisons = {}
        for role in ("host", "viewer"):
            baseline_role = baseline_run["roles"][role]
            candidate_role = candidate_run["roles"][role]
            role_dimensions = (
                baseline_role["width"] > 0
                and baseline_role["height"] > 0
                and candidate_role["width"] == baseline_role["width"]
                and candidate_role["height"] == baseline_role["height"]
            )
            metadata_keys = (
                "cadenceNum", "cadenceDen", "pixelAspectNum", "pixelAspectDen",
                "colorRange", "colorSpace", "codec", "profile",
            )
            role_metadata = all(
                candidate_role[key] == baseline_role[key]
                for key in metadata_keys
            )
            baseline_submitted = baseline_role["submitted"]
            candidate_submitted = candidate_role["submitted"]
            role_cadence = (
                candidate_submitted / baseline_submitted
                if baseline_submitted else 0.0
            )
            role_additional_drops = max(
                0,
                candidate_role["dropped"] + candidate_role["coalesced"]
                - baseline_role["dropped"] - baseline_role["coalesced"],
            )
            role_backlog = candidate_role["maxPending"] <= 1
            role_comparisons[role] = {
                "exact_dimensions": role_dimensions,
                "exact_metadata": role_metadata,
                "cadence_ratio": role_cadence,
                "additional_drops": role_additional_drops,
                "one_frame_backlog_bound": role_backlog,
            }
            exact_dimensions.append(role_dimensions)
            exact_metadata.append(role_metadata)
            cadence_ratios.append(role_cadence)
            additional_drops.append(role_additional_drops)
            backlog_bounds.append(role_backlog)
        run_role_comparisons.append({"run": index, "roles": role_comparisons})
    report = {
        "baselineRuns": baseline,
        "candidateRuns": candidate,
        "run_role_comparisons": run_role_comparisons,
        "exact_dimensions": all(exact_dimensions),
        "exact_metadata": all(exact_metadata),
        "capability_contract": _capability_contract_summary(
            [*baseline, *candidate]
        ),
        "cadence_ratio": min(cadence_ratios, default=0.0),
        "additional_drops": max(additional_drops, default=1),
        "combined_average_cpu_reduction": (
            1.0 - candidate_median_cpu / baseline_median_cpu
            if baseline_median_cpu else 0.0
        ),
        "candidate_cpu_p95_regression": candidate_median_p95 - baseline_median_p95,
        "rss_p95_growth": (
            candidate_median_rss / baseline_median_rss - 1.0
            if baseline_median_rss else 1.0
        ),
        "psnr_db": None,
        "ssim": None,
        "paused_cpu_reduction": None,
        "one_frame_backlog_bound": all(backlog_bounds),
    }
    report["gatePassed"] = gates_pass(report)
    return report


def synthetic_passing_report() -> dict:
    return {
        "exact_dimensions": True,
        "exact_metadata": True,
        "capability_contract": {
            "requested_modes": ["software"],
            "decoder_paths": ["software"],
            "webrtc_encoder": "vp8-software",
            "hardware_encoder_status": "unavailable-locked-abi",
        },
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


def _valid_capability_contract(contract: object) -> bool:
    if not isinstance(contract, dict):
        return False
    requested_modes = contract.get("requested_modes")
    decoder_paths = contract.get("decoder_paths")
    if (
        not isinstance(requested_modes, list)
        or not requested_modes
        or not all(
            isinstance(value, str) and value in ENUMS["requested_mode"]
            for value in requested_modes
        )
        or len(requested_modes) != len(set(requested_modes))
        or not isinstance(decoder_paths, list)
        or not decoder_paths
        or not all(
            isinstance(value, str) and value in ENUMS["decoder_path"]
            for value in decoder_paths
        )
        or len(decoder_paths) != len(set(decoder_paths))
    ):
        return False
    return (
        contract.get("webrtc_encoder") == "vp8-software"
        and contract.get("hardware_encoder_status")
        == "unavailable-locked-abi"
    )


def gates_pass(report: dict) -> bool:
    psnr = report.get("psnr_db")
    ssim = report.get("ssim")
    paused_reduction = report.get("paused_cpu_reduction")
    return (
        report.get("exact_dimensions") is True
        and report.get("exact_metadata") is True
        and _valid_capability_contract(report.get("capability_contract"))
        and report.get("cadence_ratio", 0) >= 0.99
        and report.get("additional_drops", 1) <= 0
        and isinstance(psnr, (int, float)) and math.isfinite(psnr) and psnr >= 45.0
        and isinstance(ssim, (int, float)) and math.isfinite(ssim) and ssim >= 0.995
        and report.get("combined_average_cpu_reduction", 0) >= 0.30
        and report.get("candidate_cpu_p95_regression", 1) <= 0
        and report.get("rss_p95_growth", 1) <= 0.10
        and isinstance(paused_reduction, (int, float))
        and math.isfinite(paused_reduction) and paused_reduction >= 0.70
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


def scenario_phase(elapsed_seconds: int) -> str:
    if elapsed_seconds < MEASUREMENT_START_SECONDS:
        return "warmup"
    if elapsed_seconds < MEASUREMENT_END_SECONDS:
        return "measurement"
    return "finalization"


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


def parse_process_metrics(stdout: str, returncode: int) -> tuple[float, int]:
    if returncode != 0:
        raise ValueError("process metrics command failed")
    fields = stdout.split()
    if len(fields) < 2:
        raise ValueError("process metrics sample missing")
    try:
        cpu = float(fields[0])
        rss = int(fields[1]) * 1024
    except (TypeError, ValueError) as error:
        raise ValueError("process metrics sample invalid") from error
    if not math.isfinite(cpu) or cpu < 0 or rss <= 0:
        raise ValueError("process metrics sample invalid")
    return cpu, rss


def _read_process_metrics(process: subprocess.Popen[str]) -> tuple[float, int]:
    if process.poll() is not None:
        raise PerformanceStudyError("process-exited-before-metrics")
    if sys.platform == "win32":
        raise PerformanceStudyError("process-metrics-unsupported-win32")
    try:
        result = subprocess.run(
            ["ps", "-o", "%cpu=", "-o", "rss=", "-p", str(process.pid)],
            check=False, capture_output=True, text=True, timeout=1,
        )
        return parse_process_metrics(result.stdout, result.returncode)
    except subprocess.TimeoutExpired as error:
        raise PerformanceStudyError("process-metrics-command-timeout") from error
    except OSError as error:
        raise PerformanceStudyError("process-metrics-command-failed") from error
    except ValueError as error:
        raise PerformanceStudyError("process-metrics-sample-invalid") from error


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
                "phase": scenario_phase(elapsed_seconds), "role": role, **parsed,
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
    demo_sha256: str,
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
    with output_path.open("x", encoding="utf-8") as output:
        _write_jsonl_line(output, {
            "kind": "run", "version": 1, "mode": video_acceleration,
            "duration_seconds": duration_seconds, "demo_sha256": demo_sha256,
        })
        try:
            host, host_reader = _start_demo(
                build_host_command(demo, server_url, movie, video_acceleration), env
            )
            room = _wait_for_room(host_reader, host)
            viewer, viewer_reader = _start_demo(
                build_viewer_command(demo, server_url, room), env
            )
            scenario_started = time.monotonic()
            deadline = scenario_started + duration_seconds
            next_metrics = scenario_started
            while time.monotonic() < deadline:
                if viewer.poll() is not None:
                    raise PerformanceStudyError("viewer-exited-during-measurement")
                if host.poll() is not None:
                    raise PerformanceStudyError("host-exited-during-measurement")
                now = time.monotonic()
                elapsed = int(now - scenario_started)
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
                            "phase": scenario_phase(elapsed), "role": role,
                            "cpu_percent": cpu, "rss_bytes": rss,
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
                "scenario_seconds": duration_seconds,
                "phase_boundaries_seconds": [30, 150],
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
    demo_sha256 = _sha256_file(args.demo)
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
                demo_sha256,
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
