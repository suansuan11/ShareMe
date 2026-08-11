#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


_SCRIPT_DIRECTORY = str(Path(__file__).resolve().parent)
if _SCRIPT_DIRECTORY not in sys.path:
    sys.path.insert(0, _SCRIPT_DIRECTORY)

from run_screen_stream_smoke import (  # noqa: E402
    PROFILE_BOUNDS,
    SmokeRuntimeError,
    run_smoke,
)
from run_signaled_call_smoke import (  # noqa: E402
    popen_group_options,
    terminate_process_group,
)


class AcceptanceError(RuntimeError):
    pass


def screen_encoder_for_mode(mode: str) -> str:
    if mode == "software-baseline":
        return "software"
    if mode == "hardware":
        return "auto"
    raise ValueError("invalid-mode")


def validate_run_request(mode: str, profile: str, duration_seconds: int) -> None:
    encoder = screen_encoder_for_mode(mode)
    if profile not in PROFILE_BOUNDS:
        raise ValueError("invalid-profile")
    if duration_seconds < 31 or duration_seconds > 3600:
        raise ValueError("invalid-duration")
    if encoder == "software" and profile != "standard":
        raise ValueError("software-standard-only")


def validate_artifact_path(artifact: Path, output_root: Path) -> None:
    target = artifact.resolve()
    root = output_root.resolve()
    try:
        target.relative_to(root)
    except ValueError as error:
        raise AcceptanceError("artifact-outside-output-root") from error
    if target.suffix.lower() != ".jsonl":
        raise AcceptanceError("artifact-must-be-jsonl")
    if target.exists():
        raise AcceptanceError("artifact-exists")


def validate_comparison_path(comparison: Path, output_root: Path) -> None:
    target = comparison.resolve()
    root = output_root.resolve()
    try:
        target.relative_to(root)
    except ValueError as error:
        raise AcceptanceError("comparison-outside-output-root") from error
    if target.suffix.lower() != ".json":
        raise AcceptanceError("comparison-must-be-json")
    if target.exists():
        raise AcceptanceError("comparison-exists")


def measurement_window(samples: list[dict], start_s: int = 30,
                       end_s: int = 150) -> list[dict]:
    selected = [
        sample for sample in samples
        if start_s <= sample["elapsed_seconds"] <= end_s
    ]
    if (not selected or selected[0]["elapsed_seconds"] > start_s or
            selected[-1]["elapsed_seconds"] < end_s):
        raise AcceptanceError("measurement-window-incomplete")
    return selected


def percentile(values: list[float | int], percentile_value: float):
    ordered = sorted(values)
    if not ordered:
        raise AcceptanceError("percentile-input-empty")
    index = math.ceil((percentile_value / 100.0) * len(ordered)) - 1
    return ordered[max(0, min(index, len(ordered) - 1))]


def median_of_three(values: list[float]) -> float:
    if len(values) != 3:
        raise AcceptanceError("three-runs-required")
    return statistics.median(values)


def _read_jsonl(path: Path) -> list[dict]:
    records = []
    try:
        with path.open(encoding="utf-8") as source:
            for line in source:
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise AcceptanceError("artifact-record-invalid")
                records.append(value)
    except (OSError, json.JSONDecodeError) as error:
        raise AcceptanceError("artifact-read-failed") from error
    return records


def _role_metrics(records: list[dict], role: str, end_s: int) -> dict:
    samples = [
        record for record in records
        if record.get("kind") == "process" and record.get("role") == role
    ]
    window = measurement_window(samples, 30, end_s)
    cpu = [float(sample["cpu_percent"]) for sample in window]
    rss = [int(sample["rss_bytes"]) for sample in window]
    return {
        "sampleCount": len(window),
        "cpuMean": round(statistics.fmean(cpu), 4),
        "cpuP95": round(float(percentile(cpu, 95)), 4),
        "rssP95": int(percentile(rss, 95)),
    }


def summarize_run(path: Path, expected_mode: str) -> dict:
    records = _read_jsonl(path)
    run = next((record for record in records if record.get("kind") == "run"),
               None)
    summary = next((record for record in reversed(records)
                    if record.get("kind") == "summary"), None)
    lifecycle = next((record for record in reversed(records)
                      if record.get("kind") == "acceptance"), None)
    if run is None or summary is None or summary.get("complete") is not True:
        raise AcceptanceError("run-incomplete")
    if (lifecycle is None or lifecycle.get("fixture_started") is not True or
            lifecycle.get("fixture_stopped") is not True):
        raise AcceptanceError("fixture-lifecycle-incomplete")
    encoder = screen_encoder_for_mode(expected_mode)
    if run.get("screen_encoder") != encoder:
        raise AcceptanceError("encoder-mode-mismatch")
    if expected_mode == "software-baseline":
        truthful = (
            summary.get("webrtc_encoder") == "VP8" and
            summary.get("encoder_implementation") == "VP8Template" and
            summary.get("hardware_encoder_status") ==
            "fallback:explicit-software"
        )
    else:
        truthful = (
            summary.get("webrtc_encoder") == "H264" and
            summary.get("encoder_implementation") == "MediaFoundation" and
            summary.get("hardware_encoder_status") == "active"
        )
    if not truthful:
        raise AcceptanceError("encoder-classification-mismatch")

    profile = run.get("profile")
    if profile not in PROFILE_BOUNDS:
        raise AcceptanceError("profile-missing")
    width, height, _ = PROFILE_BOUNDS[profile]
    host_media = summary.get("host", {})
    viewer_media = summary.get("viewer", {})
    exact_geometry = (
        (host_media.get("width"), host_media.get("height")) == (width, height)
        and (viewer_media.get("width"), viewer_media.get("height")) ==
        (width, height)
    )
    host_callback = host_media.get("callback", 0)
    host_submitted = host_media.get("submitted", 0)
    host_encoded = host_media.get("encoded", 0)
    viewer_submitted = viewer_media.get("submitted", 0)
    cadence = min(
        host_submitted / host_callback if host_callback else 0.0,
        viewer_submitted / host_encoded if host_encoded else 0.0,
    )
    quality_passed = (
        exact_geometry and cadence >= 0.95 and
        viewer_media.get("presentation_recovery_count") == 1 and
        viewer_media.get("voice_packets_sent", 0) > 0 and
        viewer_media.get("voice_packets_received", 0) > 0
    )
    duration_seconds = int(run.get("duration_seconds", 180))
    end_s = min(150, duration_seconds - 1)
    if end_s < 30:
        raise AcceptanceError("measurement-window-incomplete")
    digest = run.get("demo_sha256")
    if not isinstance(digest, str) or len(digest) != 64:
        raise AcceptanceError("binary-identity-missing")
    return {
        "artifact": path.name,
        "mode": expected_mode,
        "profile": profile,
        "demoSha256": digest,
        "host": _role_metrics(records, "host", end_s),
        "viewer": _role_metrics(records, "viewer", end_s),
        "cadenceRatio": round(cadence, 6),
        "qualityPassed": quality_passed,
        "width": width,
        "height": height,
    }


def compare_standard(baseline_runs: list[dict],
                     hardware_runs: list[dict]) -> dict:
    if len(baseline_runs) != 3 or len(hardware_runs) != 3:
        raise AcceptanceError("three-runs-required")
    all_runs = [*baseline_runs, *hardware_runs]
    identities = {run.get("demoSha256") for run in all_runs}
    if len(identities) != 1:
        raise AcceptanceError("binary-identity-mismatch")
    if any(run.get("profile") != "standard" for run in all_runs):
        raise AcceptanceError("standard-profile-required")
    baseline_cpu = median_of_three(
        [float(run["host"]["cpuMean"]) for run in baseline_runs]
    )
    hardware_cpu = median_of_three(
        [float(run["host"]["cpuMean"]) for run in hardware_runs]
    )
    baseline_cpu_p95 = median_of_three(
        [float(run["host"]["cpuP95"]) for run in baseline_runs]
    )
    hardware_cpu_p95 = median_of_three(
        [float(run["host"]["cpuP95"]) for run in hardware_runs]
    )
    baseline_rss = median_of_three(
        [float(run["host"]["rssP95"]) for run in baseline_runs]
    )
    hardware_rss = median_of_three(
        [float(run["host"]["rssP95"]) for run in hardware_runs]
    )
    reduction = ((baseline_cpu - hardware_cpu) / baseline_cpu
                 if baseline_cpu else 0.0)
    rss_growth = ((hardware_rss - baseline_rss) / baseline_rss
                  if baseline_rss else 1.0)
    cadence = min(float(run.get("cadenceRatio", 0)) for run in hardware_runs)
    quality = all(run.get("qualityPassed") is True for run in all_runs)
    accepted = (
        reduction >= 0.30 and hardware_cpu_p95 <= baseline_cpu_p95 and
        rss_growth <= 0.15 and cadence >= 0.95 and quality
    )
    return {
        "schema": "windows-screen-comparison-v1",
        "accepted": accepted,
        "demoSha256": identities.pop(),
        "cpuReduction": round(reduction, 6),
        "baselineHostCpuMean": baseline_cpu,
        "hardwareHostCpuMean": hardware_cpu,
        "baselineHostCpuP95": baseline_cpu_p95,
        "hardwareHostCpuP95": hardware_cpu_p95,
        "rssP95Growth": round(rss_growth, 6),
        "hardwareCadenceRatio": cadence,
        "qualityPassed": quality,
    }


def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(payload, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        temporary.replace(path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _find_program(repo_root: Path, name: str) -> Path:
    candidates = (
        repo_root / "build" / "call-dev" / f"{name}.exe",
        repo_root / "build" / "call-dev" / "client" / "tools" / name /
        f"{name}.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise AcceptanceError(f"{name}-not-found")


def run_acceptance(*, demo: Path, fixture: Path, server_root: Path,
                   mode: str, profile: str, duration_seconds: int, port: int,
                   artifact: Path) -> dict:
    validate_run_request(mode, profile, duration_seconds)
    fixture_environment = os.environ.copy()
    fixture_environment.pop("QT_QPA_PLATFORM", None)
    fixture_process = None
    fixture_started = False
    fixture_stopped = False
    try:
        fixture_process = subprocess.Popen(
            [str(fixture), "--profile", profile, "--duration-seconds",
             str(min(3600, duration_seconds + 30))],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            env=fixture_environment, **popen_group_options(),
        )
        time.sleep(1)
        if fixture_process.poll() is not None:
            raise AcceptanceError("fixture-early-exit")
        fixture_started = True
        run_smoke(
            demo=demo,
            server_root=server_root,
            profile=profile,
            duration_seconds=duration_seconds,
            port=port,
            artifact=artifact,
            allow_software_fallback=(mode == "software-baseline"),
            screen_encoder=screen_encoder_for_mode(mode),
            guard_processes=(("fixture", fixture_process),),
        )
    except (OSError, SmokeRuntimeError, ValueError) as error:
        raise AcceptanceError(str(error)) from error
    finally:
        if fixture_process is not None:
            terminate_process_group(fixture_process, grace_seconds=1)
            fixture_stopped = fixture_process.poll() is not None
        if artifact.exists():
            with artifact.open("a", encoding="utf-8") as output:
                output.write(json.dumps({
                    "kind": "acceptance",
                    "fixture_started": fixture_started,
                    "fixture_stopped": fixture_stopped,
                }, sort_keys=True, separators=(",", ":")) + "\n")
    summary = summarize_run(artifact, mode)
    if not summary["qualityPassed"]:
        raise AcceptanceError("quality-gate-failed")
    return summary


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("software-baseline", "hardware"))
    parser.add_argument("--profile", choices=tuple(PROFILE_BOUNDS))
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument("--port", type=int, default=18201)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--output-root", type=Path,
                        default=repo_root / "out")
    parser.add_argument("--demo", type=Path)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--server-root", type=Path,
                        default=repo_root / "server")
    parser.add_argument("--compare-baseline", type=Path, nargs=3)
    parser.add_argument("--compare-hardware", type=Path, nargs=3)
    parser.add_argument("--comparison", type=Path)
    args = parser.parse_args()
    try:
        if args.compare_baseline or args.compare_hardware or args.comparison:
            if not (args.compare_baseline and args.compare_hardware and
                    args.comparison):
                raise AcceptanceError("comparison-arguments-incomplete")
            validate_comparison_path(args.comparison, args.output_root)
            baseline = [summarize_run(path, "software-baseline")
                        for path in args.compare_baseline]
            hardware = [summarize_run(path, "hardware")
                        for path in args.compare_hardware]
            comparison = compare_standard(baseline, hardware)
            atomic_write_json(args.comparison, comparison)
            print(json.dumps(comparison, sort_keys=True))
            return 0 if comparison["accepted"] else 1
        if (args.mode is None or args.profile is None or
                args.duration_seconds is None or args.artifact is None):
            raise AcceptanceError("run-arguments-incomplete")
        validate_artifact_path(args.artifact, args.output_root)
        demo = (args.demo or _find_program(repo_root, "shareme_rtc_demo")).resolve()
        fixture = (args.fixture or _find_program(
            repo_root, "shareme_screen_motion_fixture"
        )).resolve()
        summary = run_acceptance(
            demo=demo, fixture=fixture,
            server_root=args.server_root.resolve(), mode=args.mode,
            profile=args.profile, duration_seconds=args.duration_seconds,
            port=args.port, artifact=args.artifact.resolve(),
        )
        print(json.dumps(summary, sort_keys=True))
        return 0
    except (AcceptanceError, ValueError) as error:
        print(f"WINDOWS_SCREEN_ACCEPTANCE_ERROR {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
