#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ctypes
import dataclasses
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable


_SCRIPT_DIRECTORY = str(Path(__file__).resolve().parent)
if _SCRIPT_DIRECTORY not in sys.path:
    sys.path.insert(0, _SCRIPT_DIRECTORY)

from process_metrics import ProcessMetricsError, ProcessSampler, summarize_samples
from run_gui_call_smoke import (
    GUI_PROBE_STATES,
    GUI_SMOKE_PROBE_COUNT,
    GuiSmokeFailure,
    run_probes,
)


PROFILES = ("standard", "quality", "cinema")
SURFACES = ("home", "create", "join", "settings", "help", "call",
            "details", "recovery")
DPI_SCALES = (100, 125, 150, 200)
PROBE_STATES = GUI_PROBE_STATES
_FIXTURE_RESULT = re.compile(
    r"SCREEN_MOTION_FIXTURE status=completed profile=(\w+) frames=(\d+)"
)


class AcceptanceError(RuntimeError):
    pass


def validate_fixture_arguments(profile: str, duration_seconds: int) -> None:
    if profile not in PROFILES:
        raise ValueError("invalid-profile")
    if duration_seconds < 1 or duration_seconds > 3600:
        raise ValueError("invalid-duration")


def _program_arguments(program: Path) -> list[str]:
    return ([sys.executable, str(program)]
            if program.suffix.lower() == ".py" else [str(program)])


def current_dpi_percent() -> int | None:
    if sys.platform != "win32":
        return None
    try:
        dpi = ctypes.windll.user32.GetDpiForSystem()
    except (AttributeError, OSError):
        return None
    return round(dpi * 100 / 96) if dpi > 0 else None


def build_manual_checklist(dpi_percent: int | None) -> dict:
    return {
        "surfaces": [
            {"surface": surface, "status": "not-run"}
            for surface in SURFACES
        ],
        "dpiScales": [
            {
                "scalePercent": scale,
                "status": ("available" if dpi_percent == scale
                           else "environment-dependent"),
            }
            for scale in DPI_SCALES
        ],
        "checks": [
            "minimum-size", "keyboard-focus", "microphone-toggle",
            "speaker-toggle", "leave", "return-home",
        ],
    }


def atomic_write_json(path: Path, payload: dict,
                      before_replace: Callable[[], None] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(payload, output, ensure_ascii=False, indent=2,
                      sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        if before_replace is not None:
            before_replace()
        temporary.replace(path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _stop_process(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def run_fixture(fixture: Path, profile: str, duration_seconds: int,
                offscreen: bool) -> tuple[dict, dict]:
    validate_fixture_arguments(profile, duration_seconds)
    environment = os.environ.copy()
    if offscreen:
        environment["QT_QPA_PLATFORM"] = "offscreen"
    arguments = _program_arguments(fixture) + [
        "--profile", profile,
        "--duration-seconds", str(duration_seconds),
    ]
    process = subprocess.Popen(
        arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="strict", env=environment,
    )
    sampler = None
    samples = []
    try:
        sampler = ProcessSampler(process.pid)
        deadline = time.monotonic() + duration_seconds + 5
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.25)
            if process.poll() is None:
                samples.append(sampler.sample())
        if process.poll() is None:
            raise AcceptanceError("fixture-timeout")
        stdout, _ = process.communicate(timeout=1)
        if process.returncode != 0:
            raise AcceptanceError("fixture-exit")
        match = _FIXTURE_RESULT.search(stdout)
        if match is None or match.group(1) != profile:
            raise AcceptanceError("fixture-contract")
        if not samples:
            raise AcceptanceError("process-samples-missing")
        return ({"profile": profile, "frames": int(match.group(2))},
                summarize_samples(samples))
    except UnicodeDecodeError as error:
        raise AcceptanceError("fixture-decode") from error
    except ProcessMetricsError as error:
        raise AcceptanceError(error.category) from error
    finally:
        if sampler is not None:
            sampler.close()
        _stop_process(process)
        if process.stdout is not None:
            process.stdout.close()
        if process.stderr is not None:
            process.stderr.close()


def acceptance_artifact(probes: list[dict], fixture: dict,
                        process_summary: dict, dpi_percent: int | None) -> dict:
    return {
        "schema": "windows-gui-acceptance-v1",
        "status": "automated-verified",
        "platform": sys.platform,
        "probes": probes,
        "fixture": fixture,
        "process": process_summary,
        "manual": build_manual_checklist(dpi_percent),
        "privacy": {
            "absolutePaths": "excluded",
            "roomIds": "excluded",
            "rawCommandLines": "excluded",
        },
    }


def failure_artifact(error: Exception, partial_probes: list[dict]) -> dict:
    if isinstance(error, GuiSmokeFailure):
        category = error.category
        probes = [dataclasses.asdict(item) for item in error.partial]
    else:
        category = str(error)
        probes = partial_probes
    return {
        "schema": "windows-gui-acceptance-v1",
        "status": "failed",
        "failure": category,
        "probes": probes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--profile", default="standard")
    parser.add_argument("--duration-seconds", type=int, default=3)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--offscreen", action="store_true")
    args = parser.parse_args()
    try:
        validate_fixture_arguments(args.profile, args.duration_seconds)
        demo = args.demo.resolve()
        fixture = args.fixture.resolve()
        if not demo.is_file() or not fixture.is_file():
            raise ValueError("missing-program")
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    partial_probes: list[dict] = []
    try:
        results = run_probes(demo, PROBE_STATES, 5.0)
        if len(results) != GUI_SMOKE_PROBE_COUNT:
            raise AcceptanceError("probe-count")
        partial_probes = [dataclasses.asdict(item) for item in results]
        fixture_result, process_summary = run_fixture(
            fixture, args.profile, args.duration_seconds, args.offscreen
        )
        payload = acceptance_artifact(
            partial_probes, fixture_result, process_summary,
            current_dpi_percent(),
        )
        atomic_write_json(args.artifact, payload)
        print("WINDOWS_GUI_ACCEPTANCE status=automated-verified "
              f"probes={len(partial_probes)} "
              f"samples={process_summary['sampleCount']}")
        return 0
    except (AcceptanceError, GuiSmokeFailure, subprocess.TimeoutExpired,
            UnicodeDecodeError) as error:
        if isinstance(error, UnicodeDecodeError):
            error = AcceptanceError("fixture-decode")
        payload = failure_artifact(error, partial_probes)
        atomic_write_json(args.artifact, payload)
        category = payload["failure"]
        print(f"WINDOWS_GUI_ACCEPTANCE status=failed category={category}",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
