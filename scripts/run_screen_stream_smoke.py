#!/usr/bin/env python3

"""Run a bounded desktop screen-stream smoke call and record sanitized evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import queue
import re
import signal
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from process_metrics import ProcessMetricsError, ProcessSampler  # noqa: E402
from run_signaled_call_smoke import (  # noqa: E402
    attach_windows_kill_job,
    cleanup_temporary_directory,
    popen_group_options,
    start_signaling_server,
    terminate_process_group,
    wait_for_health,
)


PROFILE_BOUNDS = {
    "standard": (1920, 1080, 60),
    "quality": (2560, 1440, 60),
    "cinema": (3840, 2160, 30),
}
ROOM_PATTERN = re.compile(r"^ROOM ([A-Z2-7]{6})$")
INTEGER_KEYS = {
    "version",
    "cpu_percent",
    "rss_bytes",
    "width",
    "height",
    "offered",
    "encoded",
    "received",
    "decoded",
    "callback",
    "submitted",
    "bytes_sent",
    "bytes_received",
    "voice_packets_sent",
    "voice_packets_received",
    "voice_bytes_sent",
    "voice_bytes_received",
    "bitrate_bps",
    "coalesced",
    "dropped",
    "max_pending",
    "conversion_failures",
    "fallback_copies",
    "source_pending",
    "source_pending_bytes",
    "source_peak_pending",
    "source_peak_pending_bytes",
    "session_video_pending",
    "session_video_bytes",
    "session_audio_pending",
    "session_audio_bytes",
    "render_queue",
    "pending_callbacks",
    "pending_callback_bytes",
    "owned_bytes",
    "owned_peak_bytes",
    "backpressure_events",
    "stats_unavailable",
    "cadence_num",
    "cadence_den",
    "pixel_aspect_num",
    "pixel_aspect_den",
    "presentation_epoch",
    "presentation_recovery_count",
}
ALLOWED_KEYS = INTEGER_KEYS | {
    "role",
    "color_range",
    "color_space",
    "codec",
    "profile",
    "requested_mode",
    "decoder_path",
    "webrtc_encoder",
    "hardware_encoder_status",
    "encoder_implementation",
    "state",
    "candidate",
}
SENSITIVE_WORD_PATTERN = re.compile(r"\b(?:ROOM|TOKEN|SDP|ICE)\b", re.IGNORECASE)


class SmokeRuntimeError(RuntimeError):
    pass


class MotionInterruption(NamedTuple):
    after_seconds: int
    duration_seconds: int


def validate_motion_interruption(
    interruption: MotionInterruption | None,
    *,
    duration_seconds: int,
    motion_fixture: Path | None,
    requested: bool = False,
) -> None:
    if interruption is None:
        if requested:
            raise SmokeRuntimeError("motion-interruption-options-must-be-paired")
        return
    if sys.platform != "darwin":
        raise SmokeRuntimeError("motion-interruption-is-macos-only")
    if motion_fixture is None:
        raise SmokeRuntimeError("motion-interruption-requires-motion-fixture")
    if interruption.after_seconds < 5:
        raise SmokeRuntimeError("motion-interruption-needs-warmup")
    if not 1 <= interruption.duration_seconds <= 5:
        raise SmokeRuntimeError("motion-interruption-duration-out-of-range")
    resume_seconds = (
        interruption.after_seconds + interruption.duration_seconds
    )
    if duration_seconds - resume_seconds < 10:
        raise SmokeRuntimeError("motion-interruption-needs-post-recovery-window")


def _signal_motion_fixture(process, fixture_signal: int, failure: str) -> None:
    if sys.platform != "darwin":
        raise SmokeRuntimeError("motion-interruption-is-macos-only")
    if process.poll() is not None:
        raise SmokeRuntimeError("motion-fixture-early-exit")
    try:
        os.kill(process.pid, fixture_signal)
    except OSError as error:
        raise SmokeRuntimeError(failure) from error


def suspend_motion_fixture(process) -> None:
    _signal_motion_fixture(
        process, signal.SIGSTOP, "motion-fixture-suspend-failed"
    )


def resume_motion_fixture(process) -> None:
    _signal_motion_fixture(
        process, signal.SIGCONT, "motion-fixture-resume-failed"
    )


def redact_diagnostic(message: str) -> str:
    redacted = re.sub(r"/(?:[^\s/]+/)+[^\s]+", "[path-redacted]", message)
    redacted = re.sub(
        r"\b[A-Za-z]:\\(?:[^\s\\]+\\)+[^\s\\]+",
        "[path-redacted]",
        redacted,
    )
    redacted = re.sub(r"\b(?:ROOM|room)\s+[A-Z2-7]{6}\b", "[redacted]", redacted)
    redacted = re.sub(
        r"\b(?:ROOM|room|TOKEN|token|SDP|sdp|ICE|ice)\b[^\s]*",
        "[redacted]",
        redacted,
    )
    return redacted


def profile_bounds(profile: str) -> tuple[int, int, int]:
    try:
        return PROFILE_BOUNDS[profile]
    except KeyError as error:
        raise ValueError(f"unsupported screen profile: {profile}") from error


def build_host_command(
    demo: Path, server_url: str, profile: str, screen_encoder: str = "auto"
) -> list[str]:
    return [
        str(demo),
        "--server",
        server_url,
        "--role",
        "host",
        "--source",
        "screen",
        "--screen-profile",
        profile,
        "--screen-encoder",
        screen_encoder,
        "--audio",
        "synthetic",
        "--no-audio-playout",
    ]


def build_viewer_command(
    demo: Path, server_url: str, room: str
) -> list[str]:
    return [
        str(demo),
        "--server",
        server_url,
        "--role",
        "viewer",
        "--room",
        room,
        "--source",
        "screen",
        "--audio",
        "synthetic",
        "--no-audio-playout",
    ]


def build_motion_fixture_command(
    fixture: Path, profile: str, duration_seconds: int
) -> list[str]:
    return [
        str(fixture),
        "--profile",
        profile,
        "--duration-seconds",
        str(min(3600, duration_seconds + 30)),
    ]


def parse_counter_line(line: str) -> dict | None:
    fields = line.strip().split()
    if not fields or fields[0] != "PERF_COUNTERS":
        return None
    parsed: dict[str, object] = {}
    try:
        for field in fields[1:]:
            key, value = field.split("=", 1)
            if key in parsed or key not in ALLOWED_KEYS:
                return None
            if key in INTEGER_KEYS:
                parsed[key] = int(value, 10)
                if parsed[key] < 0:
                    return None
            else:
                if not re.fullmatch(r"[A-Za-z0-9_.:+-]+", value):
                    return None
                parsed[key] = value
    except (TypeError, ValueError):
        return None
    if (
        parsed.get("version") != 1
        or parsed.get("role") not in {"host", "viewer"}
        or SENSITIVE_WORD_PATTERN.search(line) is not None
    ):
        return None
    return parsed


def _latest(records: list[dict], role: str) -> dict:
    matching = [record for record in records if record.get("role") == role]
    if not matching:
        raise SmokeRuntimeError(f"missing {role} performance counters")
    return matching[-1]


def _require_positive(record: dict, keys: tuple[str, ...], role: str) -> None:
    if any(not isinstance(record.get(key), int) or record[key] <= 0 for key in keys):
        raise SmokeRuntimeError(f"{role} media counters are incomplete")


def _last_positive(records: list[dict], key: str) -> int:
    for record in reversed(records):
        value = record.get(key)
        if isinstance(value, int) and value > 0:
            return value
    return 0


def _validate_dimensions(record: dict, profile: str, role: str) -> None:
    max_width, max_height, _ = profile_bounds(profile)
    width = record.get("width")
    height = record.get("height")
    if (
        not isinstance(width, int)
        or not isinstance(height, int)
        or width <= 0
        or height <= 0
        or width > max_width
        or height > max_height
        or width % 2
        or height % 2
    ):
        raise SmokeRuntimeError(f"{role} dimensions exceed {profile} bounds")


def _validate_queue_and_conversion(record: dict, role: str) -> None:
    if record.get("max_pending") != 1:
        raise SmokeRuntimeError(f"{role} presentation queue is not bounded to one")
    if record.get("conversion_failures", 0) != 0:
        raise SmokeRuntimeError(f"{role} reported conversion failures")


VOICE_COUNTER_KEYS = (
    "voice_packets_sent",
    "voice_packets_received",
    "voice_bytes_sent",
    "voice_bytes_received",
)


def _validate_continuous_progress(
    records: list[dict], role: str, video_keys: tuple[str, ...]
) -> dict:
    matching = [record for record in records if record.get("role") == role]
    required_keys = video_keys + VOICE_COUNTER_KEYS
    ready_index = next(
        (
            index
            for index, record in enumerate(matching)
            if record.get("stats_unavailable") == 0
            and all(isinstance(record.get(key), int) and record[key] > 0
                    for key in required_keys)
        ),
        None,
    )
    if ready_index is None:
        raise SmokeRuntimeError(f"{role} continuity counters never became ready")
    observed = matching[ready_index:]
    if any(record.get("stats_unavailable") != 0 for record in observed):
        raise SmokeRuntimeError(f"{role} media stats became unavailable after warmup")

    max_stalls: dict[str, int] = {}
    for key in required_keys:
        previous: int | None = None
        current_stall = 0
        maximum_stall = 0
        for record in observed:
            value = record.get(key)
            if not isinstance(value, int) or value <= 0:
                raise SmokeRuntimeError(f"{role} {key} continuity is incomplete")
            if previous is not None:
                if value < previous:
                    raise SmokeRuntimeError(f"{role} {key} regressed")
                if value == previous:
                    current_stall += 1
                    maximum_stall = max(maximum_stall, current_stall)
                else:
                    current_stall = 0
            previous = value
        if maximum_stall > 5:
            raise SmokeRuntimeError(f"{role} {key} stalled for too long")
        max_stalls[key] = maximum_stall
    return {
        "warmup_samples": ready_index,
        "observed_samples": len(observed),
        "max_stall_samples": max(max_stalls.values(), default=0),
    }


def _validate_presentation_recovery(records: list[dict]) -> dict:
    matching = [record for record in records if record.get("role") == "viewer"]
    recovery_index = next(
        (
            index
            for index, record in enumerate(matching)
            if record.get("presentation_recovery_count") == 1
            and record.get("presentation_epoch") == 1
        ),
        None,
    )
    if recovery_index is None or recovery_index == 0:
        raise SmokeRuntimeError("viewer presentation recovery was not observed")
    if any(
        record.get("presentation_recovery_count") not in {0, 1}
        or record.get("presentation_epoch") not in {0, 1}
        for record in matching
    ):
        raise SmokeRuntimeError("viewer presentation recovery count is invalid")
    before_submitted = matching[recovery_index - 1].get("submitted", 0)
    final_submitted = matching[-1].get("submitted", 0)
    if (
        not isinstance(before_submitted, int)
        or not isinstance(final_submitted, int)
        or final_submitted <= before_submitted
    ):
        raise SmokeRuntimeError("viewer presentation did not progress after recovery")
    return {
        "presentation_epoch": 1,
        "presentation_recovery_count": 1,
        "post_recovery_submissions": final_submitted - before_submitted,
    }


def validate_records(
    profile: str,
    host_records: list[dict],
    viewer_records: list[dict],
    *,
    require_hardware: bool = True,
) -> dict:
    host = _latest(host_records, "host")
    viewer = _latest(viewer_records, "viewer")
    capture_profile = profile if require_hardware else "standard"
    _validate_dimensions(host, capture_profile, "host")
    _validate_dimensions(viewer, capture_profile, "viewer")
    if (host["width"], host["height"]) != (viewer["width"], viewer["height"]):
        raise SmokeRuntimeError("host and viewer geometry do not match")
    _require_positive(host, ("encoded", "callback", "submitted"), "host")
    _require_positive(viewer, ("received", "decoded", "callback", "submitted"), "viewer")
    _validate_queue_and_conversion(host, "host")
    _validate_queue_and_conversion(viewer, "viewer")
    host_continuity = _validate_continuous_progress(
        host_records, "host", ("encoded", "callback", "submitted")
    )
    viewer_continuity = _validate_continuous_progress(
        viewer_records, "viewer", ("received", "decoded", "callback", "submitted")
    )
    viewer_recovery = _validate_presentation_recovery(viewer_records)
    host_bitrate = _last_positive(host_records, "bitrate_bps")
    viewer_bitrate = _last_positive(viewer_records, "bitrate_bps")
    if require_hardware:
        if host.get("hardware_encoder_status") != "active":
            raise SmokeRuntimeError(
                "host did not maintain hardware VideoToolbox encoding"
            )
        if host.get("webrtc_encoder") != "H264":
            raise SmokeRuntimeError("host did not report negotiated H264")
        expected_implementation = (
            "MediaFoundation" if sys.platform == "win32" else "VideoToolbox"
        )
        if host.get("encoder_implementation") != expected_implementation:
            raise SmokeRuntimeError(
                "host did not report the native hardware encoder implementation"
            )
    else:
        hardware_status = host.get("hardware_encoder_status")
        if (
            not isinstance(hardware_status, str)
            or not hardware_status.startswith("fallback:")
        ):
            raise SmokeRuntimeError("host did not report an encoder fallback")
        if host.get("webrtc_encoder") != "VP8":
            raise SmokeRuntimeError("host did not report negotiated VP8 fallback")
    if host_bitrate <= 0 or viewer_bitrate <= 0:
        raise SmokeRuntimeError("video bitrate was not measured")
    return {
        "profile": profile,
        "hardware_encoder_status": host["hardware_encoder_status"],
        "webrtc_encoder": host["webrtc_encoder"],
        "encoder_implementation": host["encoder_implementation"],
        "host": {
            "width": host["width"],
            "height": host["height"],
            "encoded": host["encoded"],
            "callback": host["callback"],
            "submitted": host["submitted"],
            "bitrate_bps": host_bitrate,
            "voice_packets_sent": host["voice_packets_sent"],
            "voice_packets_received": host["voice_packets_received"],
            "voice_bytes_sent": host["voice_bytes_sent"],
            "voice_bytes_received": host["voice_bytes_received"],
            "continuity": host_continuity,
        },
        "viewer": {
            "width": viewer["width"],
            "height": viewer["height"],
            "received": viewer["received"],
            "decoded": viewer["decoded"],
            "callback": viewer["callback"],
            "submitted": viewer["submitted"],
            "bitrate_bps": viewer_bitrate,
            "voice_packets_sent": viewer["voice_packets_sent"],
            "voice_packets_received": viewer["voice_packets_received"],
            "voice_bytes_sent": viewer["voice_bytes_sent"],
            "voice_bytes_received": viewer["voice_bytes_received"],
            "continuity": viewer_continuity,
            **viewer_recovery,
        },
    }


class OutputReader:
    def __init__(self, process: subprocess.Popen[str]):
        self.events: queue.Queue[str | None] = queue.Queue()
        self.lines: list[str] = []
        self.process = process
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        stream = self.process.stdout
        if stream is None:
            self.events.put(None)
            return
        try:
            for line in stream:
                self.lines.append(line)
                self.events.put(line)
        finally:
            self.events.put(None)

    def join(self) -> None:
        self.thread.join(timeout=2)


def _wait_for_room(reader: OutputReader, process: subprocess.Popen[str]) -> str:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SmokeRuntimeError("host exited before creating a room")
        try:
            line = reader.events.get(timeout=0.25)
        except queue.Empty:
            continue
        if line is None:
            raise SmokeRuntimeError("host output closed before creating a room")
        match = ROOM_PATTERN.fullmatch(line.strip())
        if match:
            return match.group(1)
    raise SmokeRuntimeError("screen smoke room timeout")


def _process_exit_diagnostic(
    host: subprocess.Popen[str],
    viewer: subprocess.Popen[str],
    host_reader: OutputReader,
    viewer_reader: OutputReader,
) -> str:
    host_reader.join()
    viewer_reader.join()
    details = []
    for role, process, reader in (
        ("host", host, host_reader),
        ("viewer", viewer, viewer_reader),
    ):
        if process.poll() is None:
            continue
        nonempty = [line.strip() for line in reader.lines if line.strip()]
        excerpt = nonempty[:8] + nonempty[-8:]
        details.append(
            f"{role}=exit-{process.returncode}: {' | '.join(dict.fromkeys(excerpt))}"
        )
    return redact_diagnostic("; ".join(details))


def _counter_diagnostic(reader: OutputReader | None) -> str:
    if reader is None:
        return "counter-lines=0 parsed=0 keys=[]"
    lines = [
        line.strip()
        for line in reader.lines
        if line.strip().startswith("PERF_COUNTERS")
    ]
    parsed = [parse_counter_line(line) for line in lines]
    keys = sorted(
        {
            field.split("=", 1)[0]
            for line in lines[-1:]
            for field in line.split()[1:]
            if "=" in field
        }
    )
    return f"counter-lines={len(lines)} parsed={sum(value is not None for value in parsed)} keys={keys}"


def _status_diagnostic(reader: OutputReader | None) -> str:
    if reader is None:
        return "statuses=[]"
    statuses = [
        redact_diagnostic(line.strip())
        for line in reader.lines
        if line.strip().startswith("SMOKE_STATUS ")
    ]
    return f"statuses={statuses[-8:]}"


def _codec_diagnostic(reader: OutputReader | None) -> str:
    if reader is None:
        return "sdp=[]"
    codecs = [
        redact_diagnostic(line.strip())
        for line in reader.lines
        if line.strip().startswith("SMOKE_SDP ")
        or any(
            marker in line
            for marker in (
                "Failed to encode",
                "Failed to copy",
                "Failed to create",
                "Compression session",
            )
        )
    ]
    return f"codec-diagnostics={codecs[-12:]}"


def _write_jsonl(handle, value: dict) -> None:
    handle.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
    handle.flush()


def _find_demo(repo_root: Path) -> Path:
    candidates = (
        repo_root / "build" / "call-dev" / "shareme_rtc_demo.exe",
        repo_root / "build" / "movie-call-dev" / "shareme_rtc_demo.exe",
        repo_root / "build" / "call-dev" / "client" / "tools" / "rtc_demo" / "shareme_rtc_demo",
        repo_root / "build" / "movie-call-dev" / "client" / "tools" / "rtc_demo" / "shareme_rtc_demo",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SmokeRuntimeError("screen demo binary not found")


def _start_measured_demo(command: list[str], **options):
    process = subprocess.Popen(command, **options)
    if os.name == "nt":
        try:
            attach_windows_kill_job(process)
        except BaseException:
            process.kill()
            process.wait()
            raise
    return process


def start_motion_fixture(
    fixture: Path, profile: str, duration_seconds: int, environment: dict[str, str]
):
    fixture_environment = environment.copy()
    fixture_environment.pop("QT_QPA_PLATFORM", None)
    return _start_measured_demo(
        build_motion_fixture_command(fixture, profile, duration_seconds),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=fixture_environment,
        **popen_group_options(),
    )


def wait_for_motion_fixture_ready(process, readiness_seconds: float = 1.0) -> None:
    deadline = time.monotonic() + readiness_seconds
    while True:
        require_guard_processes_alive((("motion-fixture", process),))
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        time.sleep(min(0.05, remaining))


def require_guard_processes_alive(guard_processes) -> None:
    for name, process in guard_processes:
        if process.poll() is not None:
            raise SmokeRuntimeError(f"{name}-early-exit")


def run_smoke(
    *,
    demo: Path,
    server_root: Path,
    profile: str,
    duration_seconds: int,
    port: int,
    artifact: Path,
    allow_software_fallback: bool = False,
    screen_encoder: str = "auto",
    guard_processes=(),
    motion_fixture: Path | None = None,
) -> dict:
    if sys.platform not in ("darwin", "win32"):
        raise SmokeRuntimeError("screen smoke requires macOS or Windows")
    profile_bounds(profile)
    if screen_encoder not in ("auto", "software"):
        raise SmokeRuntimeError("invalid screen encoder")
    if screen_encoder == "software" and profile != "standard":
        raise SmokeRuntimeError("software screen encoding requires standard")
    if duration_seconds <= 0:
        raise SmokeRuntimeError("duration must be positive")
    if artifact.exists():
        raise SmokeRuntimeError("refusing to overwrite smoke artifact")
    if motion_fixture is not None and not motion_fixture.is_file():
        raise SmokeRuntimeError("motion-fixture-unavailable")
    max_width, max_height, max_frames_per_second = profile_bounds(profile)
    artifact.parent.mkdir(parents=True, exist_ok=True)
    address = f"127.0.0.1:{port}"
    server = server_log = server_directory = None
    host = viewer = None
    fixture_process = None
    host_reader = viewer_reader = None
    host_records: list[dict] = []
    viewer_records: list[dict] = []
    process_records: list[dict] = []
    process_samplers: dict[str, ProcessSampler] = {}
    failure: str | None = None
    summary: dict | None = None
    records_written = False
    fixture_started = False
    fixture_alive = False
    fixture_stopped = False
    environment = os.environ.copy()
    environment["SHAREME_PERFORMANCE_COUNTERS"] = "1"
    environment["SHAREME_SCREEN_SMOKE_DIAGNOSTICS"] = "1"
    environment["SHAREME_SCREEN_RECOVERY_PROBE"] = "1"

    with artifact.open("x", encoding="utf-8") as output:
        run_record = {
            "kind": "run",
            "version": 1,
            "run_id": uuid.uuid4().hex,
            "profile": profile,
            "duration_seconds": duration_seconds,
            "max_width": max_width,
            "max_height": max_height,
            "max_frames_per_second": max_frames_per_second,
            "platform": sys.platform,
            "encoder_requirement": (
                "software-fallback" if allow_software_fallback else "hardware"
            ),
            "screen_encoder": screen_encoder,
            "demo_sha256": hashlib.sha256(demo.read_bytes()).hexdigest(),
        }
        if motion_fixture is not None:
            run_record["motion_fixture_requested"] = True
        _write_jsonl(output, run_record)
        try:
            effective_guard_processes = tuple(guard_processes)
            if motion_fixture is not None:
                fixture_process = start_motion_fixture(
                    motion_fixture, profile, duration_seconds, environment
                )
                fixture_started = True
                wait_for_motion_fixture_ready(fixture_process)
                effective_guard_processes += (
                    ("motion-fixture", fixture_process),
                )
            server, server_log, server_directory = start_signaling_server(
                server_root, "127.0.0.1", port
            )
            wait_for_health(
                f"http://127.0.0.1:{port}/healthz", server, server_log=server_log
            )
            host = _start_measured_demo(
                build_host_command(
                    demo, f"ws://{address}/v1/ws", profile, screen_encoder
                ),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
                **popen_group_options(),
            )
            host_reader = OutputReader(host)
            room = _wait_for_room(host_reader, host)
            viewer = _start_measured_demo(
                build_viewer_command(demo, f"ws://{address}/v1/ws", room),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
                **popen_group_options(),
            )
            viewer_reader = OutputReader(viewer)
            process_samplers["host"] = ProcessSampler(host.pid)
            try:
                process_samplers["viewer"] = ProcessSampler(viewer.pid)
            except BaseException:
                process_samplers["host"].close()
                process_samplers.clear()
                raise
            scenario_started = time.monotonic()
            deadline = scenario_started + duration_seconds
            next_sample = scenario_started + 1.0
            sample = 0
            while time.monotonic() < deadline:
                require_guard_processes_alive(effective_guard_processes)
                if host.poll() is not None or viewer.poll() is not None:
                    diagnostic = _process_exit_diagnostic(
                        host, viewer, host_reader, viewer_reader
                    )
                    raise SmokeRuntimeError(
                        "screen smoke peer exited during call"
                        + (f": {diagnostic}" if diagnostic else "")
                    )
                now = time.monotonic()
                if now < next_sample:
                    time.sleep(min(0.25, next_sample - now))
                    continue
                for process, role in ((host, "host"), (viewer, "viewer")):
                    measured = process_samplers[role].sample()
                    process_records.append({
                        "kind": "process",
                        "role": role,
                        "sample": sample,
                        "elapsed_seconds": int(
                            time.monotonic() - scenario_started
                        ),
                        "cpu_percent": measured.cpu_percent,
                        "rss_bytes": measured.rss_bytes,
                    })
                sample += 1
                next_sample += 1.0
            if fixture_process is not None:
                fixture_alive = fixture_process.poll() is None
            terminate_process_group(viewer, grace_seconds=1)
            terminate_process_group(host, grace_seconds=1)
            host_reader.join()
            viewer_reader.join()
            for role, reader, records in (
                ("host", host_reader, host_records),
                ("viewer", viewer_reader, viewer_records),
            ):
                for line in reader.lines:
                    parsed = parse_counter_line(line)
                    if parsed is not None and parsed.get("role") == role:
                        records.append(parsed)
            for record in process_records:
                _write_jsonl(output, record)
            for role, records in (("host", host_records), ("viewer", viewer_records)):
                for index, record in enumerate(records):
                    _write_jsonl(output, {
                        "kind": "counter",
                        "role": role,
                        "sample": index,
                        **record,
                    })
            records_written = True
            summary = validate_records(
                profile,
                host_records,
                viewer_records,
                require_hardware=not allow_software_fallback,
            )
        except (OSError, ValueError, subprocess.TimeoutExpired,
                ProcessMetricsError) as error:
            failure = redact_diagnostic(str(error))
        except SmokeRuntimeError as error:
            failure = redact_diagnostic(str(error))
        except KeyboardInterrupt:
            failure = "interrupted"
        finally:
            for sampler in process_samplers.values():
                sampler.close()
            for process in (viewer, host):
                if process is not None and process.poll() is None:
                    terminate_process_group(process, grace_seconds=1)
            if server is not None and server.poll() is None:
                terminate_process_group(server, grace_seconds=1)
            if server_log is not None:
                server_log.close()
            if server_directory is not None:
                cleanup_temporary_directory(server_directory)
            if fixture_process is not None:
                if fixture_process.poll() is None:
                    terminate_process_group(fixture_process, grace_seconds=1)
                fixture_stopped = fixture_process.poll() is not None
        if summary is not None:
            if motion_fixture is not None:
                summary.update({
                    "motion_fixture_started": fixture_started,
                    "motion_fixture_alive": fixture_alive,
                    "motion_fixture_stopped": fixture_stopped,
                })
            _write_jsonl(output, {"kind": "summary", "complete": True, **summary})
            return summary
        if failure is not None:
            failure += (
                f" [host: {_status_diagnostic(host_reader)};"
                f" {_codec_diagnostic(host_reader)};"
                f" viewer: {_status_diagnostic(viewer_reader)}]"
                f" {_codec_diagnostic(viewer_reader)}"
            )
            if "performance counters" in failure:
                failure += (
                    f" [host: {_counter_diagnostic(host_reader)};"
                    f" viewer: {_counter_diagnostic(viewer_reader)}]"
                )
            if not records_written:
                for role, reader, records in (
                    ("host", host_reader, host_records),
                    ("viewer", viewer_reader, viewer_records),
                ):
                    if reader is None:
                        continue
                    reader.join()
                    for line in reader.lines:
                        parsed = parse_counter_line(line)
                        if parsed is not None and parsed.get("role") == role:
                            records.append(parsed)
                for record in process_records:
                    _write_jsonl(output, record)
                for role, records in (("host", host_records), ("viewer", viewer_records)):
                    for index, record in enumerate(records):
                        _write_jsonl(output, {
                            "kind": "counter",
                            "role": role,
                            "sample": index,
                            **record,
                        })
            failure_record = {
                "kind": "summary",
                "complete": False,
                "failure": failure,
            }
            if motion_fixture is not None:
                failure_record.update({
                    "motion_fixture_started": fixture_started,
                    "motion_fixture_alive": fixture_alive,
                    "motion_fixture_stopped": fixture_stopped,
                })
            _write_jsonl(output, failure_record)
            raise SmokeRuntimeError(failure)
    raise SmokeRuntimeError("screen smoke did not produce a result")


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, default=None)
    parser.add_argument("--server-root", type=Path, default=repo_root / "server")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--profile", choices=tuple(PROFILE_BOUNDS), required=True)
    parser.add_argument("--duration-seconds", type=int, required=True)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--motion-fixture", type=Path)
    parser.add_argument("--allow-software-fallback", action="store_true")
    parser.add_argument("--screen-encoder", choices=("auto", "software"),
                        default="auto")
    args = parser.parse_args()
    demo = args.demo or _find_demo(repo_root)
    artifact = args.artifact or (
        repo_root / "out" / "hardware-screen-streaming" /
        f"{args.profile}-{args.duration_seconds}s.jsonl"
    )
    try:
        print(json.dumps(run_smoke(
            demo=demo.resolve(),
            server_root=args.server_root.resolve(),
            profile=args.profile,
            duration_seconds=args.duration_seconds,
            port=args.port,
            artifact=artifact.resolve(),
            allow_software_fallback=(
                args.allow_software_fallback or args.screen_encoder == "software"
            ),
            screen_encoder=args.screen_encoder,
            motion_fixture=(
                args.motion_fixture.resolve()
                if args.motion_fixture is not None
                else None
            ),
        ), sort_keys=True))
        return 0
    except (SmokeRuntimeError, OSError, ValueError) as error:
        print(f"SMOKE_ERROR {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
