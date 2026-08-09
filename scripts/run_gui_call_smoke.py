#!/usr/bin/env python3

import argparse
import dataclasses
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable


@dataclasses.dataclass(frozen=True)
class ProbeResult:
    state: str
    passed: bool
    duration_ms: int


class GuiSmokeFailure(RuntimeError):
    def __init__(self, category: str, partial: list[ProbeResult]):
        super().__init__(category)
        self.category = category
        self.partial = partial


def _arguments(demo: Path, state: str) -> list[str]:
    arguments = [str(demo)]
    if state.startswith("call-"):
        role = "viewer" if state == "call-viewer" else "host"
        arguments.extend([
            "--server", "ws://127.0.0.1:18080/v1/ws",
            "--role", role,
            "--source", "test",
            "--audio", "synthetic",
            "--no-audio-playout",
        ])
        if role == "viewer":
            arguments.extend(["--room", "ABC234"])
    arguments.extend(["--gui-smoke-state", state])
    return arguments


def run_probes(demo: Path, states: Iterable[str],
               timeout_seconds: float) -> list[ProbeResult]:
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    results: list[ProbeResult] = []
    for state in states:
        started = time.monotonic()
        completed = subprocess.run(
            _arguments(demo, state),
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
            check=False,
            env=environment,
        )
        duration_ms = round((time.monotonic() - started) * 1000)
        if completed.returncode != 0:
            raise GuiSmokeFailure(f"probe-exit:{state}", results)
        expected = (
            "GUI_ACTION microphone=1 speaker=1 drawer=1 leave=1 page=home"
            if state == "call-host-actions"
            else f"GUI_STATE page={state} qml_loaded=1"
        )
        forbidden = ("TypeError:", "ReferenceError:", "Binding loop",
                     "failed to load component")
        if expected not in completed.stdout or any(
                item in completed.stderr for item in forbidden):
            raise GuiSmokeFailure(f"probe-contract:{state}", results)
        results.append(ProbeResult(state, True, duration_ms))
    return results


def sample_idle_process(demo: Path, duration_seconds: float) -> dict:
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    process = subprocess.Popen(
        [str(demo)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )
    cpu_samples: list[float] = []
    rss_samples: list[int] = []
    try:
        deadline = time.monotonic() + duration_seconds
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise GuiSmokeFailure("idle-early-exit", [])
            sampled = subprocess.run(
                ["ps", "-o", "%cpu=", "-o", "rss=", "-p", str(process.pid)],
                capture_output=True,
                text=True,
                check=False,
            )
            fields = sampled.stdout.split()
            if sampled.returncode == 0 and len(fields) == 2:
                cpu_samples.append(float(fields[0]))
                rss_samples.append(int(fields[1]))
            time.sleep(0.25)
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    if not cpu_samples or len(cpu_samples) != len(rss_samples):
        raise GuiSmokeFailure("idle-samples-missing", [])
    return {
        "sampleCount": len(cpu_samples),
        "cpuMeanPercent": round(statistics.fmean(cpu_samples), 3),
        "cpuMaxPercent": round(max(cpu_samples), 3),
        "rssMaxKiB": max(rss_samples),
    }


def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(payload, output, ensure_ascii=False, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        temporary.replace(path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--idle-sample-seconds", type=float, default=3.0)
    args = parser.parse_args()
    demo = args.demo.resolve()
    if not demo.is_file() or args.idle_sample_seconds < 1.0:
        print("invalid GUI smoke arguments", file=sys.stderr)
        return 2
    states = ("home", "create", "join", "call-host", "call-viewer",
              "call-host-actions")
    try:
        probes = run_probes(demo, states, 5.0)
        idle = sample_idle_process(demo, args.idle_sample_seconds)
        artifact = {
            "schema": "gui-call-smoke-v1",
            "status": "verified",
            "platform": sys.platform,
            "probes": [dataclasses.asdict(item) for item in probes],
            "idle": idle,
            "boundaries": {
                "nativeMedia": "separate-screen-stream-gate",
                "physicalTemperature": "unverified",
                "acousticAudibility": "unverified",
            },
        }
        atomic_write_json(args.artifact, artifact)
        print("GUI_SMOKE status=verified probes=6 idle_samples="
              f"{idle['sampleCount']}")
        return 0
    except (GuiSmokeFailure, subprocess.TimeoutExpired) as error:
        category = error.category if isinstance(error, GuiSmokeFailure) else "timeout"
        partial = error.partial if isinstance(error, GuiSmokeFailure) else []
        atomic_write_json(args.artifact, {
            "schema": "gui-call-smoke-v1",
            "status": "failed",
            "failure": category,
            "probes": [dataclasses.asdict(item) for item in partial],
        })
        print(f"GUI_SMOKE status=failed category={category}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
