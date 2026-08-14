#!/usr/bin/env python3

import argparse
import dataclasses
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable


_SCRIPT_DIRECTORY = str(Path(__file__).resolve().parent)
if _SCRIPT_DIRECTORY not in sys.path:
    sys.path.insert(0, _SCRIPT_DIRECTORY)

from process_metrics import (ProcessMetricsError, ProcessSample, ProcessSampler,
                             summarize_samples)


_GUI_OBJECTS_BY_STATE = {
    "home": ("createRoomButton", "joinRoomButton", "recentRoomAction"),
    "create": ("qualityProfileControl", "preflightPrimaryButton",
                "microphoneIntentControl", "speakerIntentControl"),
    "join": ("roomCodeField", "preflightPrimaryButton",
              "microphoneIntentControl", "speakerIntentControl"),
    "settings": ("settingsDialog",),
    "help": ("helpDialog",),
    "recovery": ("recoverySurface",),
    "call-host": ("callPage", "microphoneControl", "speakerControl",
                  "detailsControl", "leaveControl", "shareControl",
                  "connectionSection", "videoSection", "audioSection",
                  "advancedSection"),
    "call-viewer": ("callPage", "microphoneControl", "speakerControl",
                    "detailsControl", "leaveControl", "shareControl",
                    "connectionSection", "videoSection", "audioSection",
                    "advancedSection"),
}
_GUI_OBJECT_MARKERS_BY_STATE = {
    state: tuple(f"GUI_OBJECT {name}=1" for name in names)
    for state, names in _GUI_OBJECTS_BY_STATE.items()
}
for _call_state in ("call-host", "call-viewer"):
    _GUI_OBJECT_MARKERS_BY_STATE[_call_state] = tuple(
        marker if not marker.endswith("shareControl=1")
        else "GUI_OBJECT shareControl=0"
        for marker in _GUI_OBJECT_MARKERS_BY_STATE[_call_state]
    )

GUI_PROBE_STATES = (
    "home", "create", "join", "settings", "help", "recovery",
    "call-host", "call-viewer", "call-host-actions",
)
GUI_SMOKE_PROBE_COUNT = 9

_GUI_RECOVERY_MARKERS = (
    "GUI_RECOVERY_TITLE category=permission-denied title=\u9700\u8981\u68c0\u67e5\u6743\u9650",
    "GUI_RECOVERY_TITLE category=invalid-room title=\u65e0\u6cd5\u52a0\u5165\u8fd9\u4e2a\u623f\u95f4",
    "GUI_RECOVERY_TITLE category=screen-capture title=\u5c4f\u5e55\u5171\u4eab\u4e0d\u53ef\u7528",
    "GUI_RECOVERY_TITLE category=audio-device title=\u58f0\u97f3\u8bbe\u5907\u4e0d\u53ef\u7528",
    "GUI_RECOVERY_TITLE category=connection-lost title=\u8fde\u63a5\u672a\u5efa\u7acb",
    "GUI_RECOVERY_TITLE category=ICE-failed title=\u8fde\u63a5\u672a\u5efa\u7acb",
    "GUI_RECOVERY_TITLE category=timed out title=\u8fde\u63a5\u672a\u5efa\u7acb",
    "GUI_RECOVERY_TITLE category=generic-failure title=\u901a\u8bdd\u6682\u65f6\u65e0\u6cd5\u7ee7\u7eed",
)

_RECOVERY_CATEGORY_MARKER = re.compile(
    r"^(GUI_RECOVERY_TITLE category=)\S+(?= title=)"
)
_RAW_GUI_TOKEN = re.compile(
    r"(?<![A-Za-z0-9])(?:kVTParameterErr|HRESULT|NSError|ICE|SDP)"
    r"(?![A-Za-z0-9])"
)
_ABSOLUTE_PATH = re.compile(
    r"(?<![A-Za-z0-9:/])/(?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+"
    r"|(?<![A-Za-z0-9])(?:[A-Za-z]:[\\/]|\\\\)[^\s\"']+"
)
_CREDENTIAL = re.compile(
    r"(?i)(?:password|passwd|secret|token|credential|api[-_]?key)"
    r"\s*[:=]\s*\S+|(?:https?|wss?)://[^/\s:@]+:[^@\s]+@"
)


def _without_recovery_category(output: str) -> str:
    return "\n".join(
        _RECOVERY_CATEGORY_MARKER.sub(r"\1<category>", line)
        for line in output.splitlines()
    )


def _unsanitized_gui_output(stdout: str, stderr: str) -> str | None:
    output = _without_recovery_category(stdout + "\n" + stderr)
    for pattern in (_RAW_GUI_TOKEN, _ABSOLUTE_PATH, _CREDENTIAL):
        match = pattern.search(output)
        if match is not None:
            return match.group(0)
    return None


def _has_exact_lines(output: str, expected: Iterable[str]) -> bool:
    lines = output.splitlines()
    return all(marker in lines for marker in expected)


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
    arguments = ([sys.executable, str(demo)]
                 if demo.suffix.lower() == ".py" else [str(demo)])
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
        try:
            completed = subprocess.run(
                _arguments(demo, state),
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="strict",
                timeout=timeout_seconds,
                check=False,
                env=environment,
            )
        except subprocess.TimeoutExpired as error:
            raise GuiSmokeFailure("timeout", results) from error
        except UnicodeDecodeError as error:
            raise GuiSmokeFailure("decode-error", results) from error
        duration_ms = round((time.monotonic() - started) * 1000)
        if completed.returncode != 0:
            raise GuiSmokeFailure(f"probe-exit:{state}", results)
        expected = (
            "GUI_ACTION microphone=1 speaker=1 drawer=1 voice_panel=1 "
            "volume_rejected_restored=1 leave=1 page=home\n"
            "GUI_ACTION advanced_closed=1 advanced_expanded=1"
            if state == "call-host-actions"
            else f"GUI_STATE page={state} qml_loaded=1"
        )
        expected_lines = tuple(expected.splitlines())
        required_markers = _GUI_OBJECT_MARKERS_BY_STATE.get(state, ())
        if state == "recovery":
            required_markers += _GUI_RECOVERY_MARKERS
        forbidden = ("TypeError:", "ReferenceError:", "Binding loop",
                     "failed to load component", "is not a type")
        if _unsanitized_gui_output(completed.stdout, completed.stderr):
            raise GuiSmokeFailure(f"probe-sanitized:{state}", results)
        if (not _has_exact_lines(completed.stdout, expected_lines) or
                not _has_exact_lines(completed.stdout, required_markers) or
                any(
                    item in completed.stderr for item in forbidden)):
            raise GuiSmokeFailure(f"probe-contract:{state}", results)
        results.append(ProbeResult(state, True, duration_ms))
    return results


def launch_idle_demo(demo: Path) -> subprocess.Popen:
    environment = os.environ.copy()
    environment.setdefault("QT_QPA_PLATFORM", "offscreen")
    return subprocess.Popen(
        ([sys.executable, str(demo)]
         if demo.suffix.lower() == ".py" else [str(demo)]),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="strict",
        env=environment,
    )


def terminate_process(process: subprocess.Popen) -> None:
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)
    if process.stderr is not None:
        process.stderr.close()


def collect_samples(process: subprocess.Popen, sampler: ProcessSampler,
                    duration_seconds: float) -> list[ProcessSample]:
    samples: list[ProcessSample] = []
    deadline = time.monotonic() + duration_seconds
    while time.monotonic() < deadline:
        time.sleep(0.25)
        if process.poll() is not None:
            raise GuiSmokeFailure("idle-early-exit", [])
        samples.append(sampler.sample())
    return samples


def sample_idle_process(demo: Path, duration_seconds: float,
                        sampler_factory=ProcessSampler) -> dict:
    process = launch_idle_demo(demo)
    sampler = None
    try:
        sampler = sampler_factory(process.pid)
        return summarize_samples(collect_samples(process, sampler, duration_seconds))
    except ProcessMetricsError as error:
        raise GuiSmokeFailure(error.category, []) from error
    finally:
        try:
            if sampler is not None:
                sampler.close()
        finally:
            terminate_process(process)


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
    states = GUI_PROBE_STATES
    probes: list[ProbeResult] = []
    try:
        probes = run_probes(demo, states, 5.0)
        if len(probes) != GUI_SMOKE_PROBE_COUNT:
            raise GuiSmokeFailure("probe-count", probes)
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
        print(f"GUI_SMOKE status=verified probes={len(probes)} idle_samples="
              f"{idle['sampleCount']}")
        return 0
    except (GuiSmokeFailure, subprocess.TimeoutExpired, UnicodeDecodeError) as error:
        if isinstance(error, GuiSmokeFailure):
            category = error.category
            partial = error.partial or probes
        elif isinstance(error, subprocess.TimeoutExpired):
            category = "timeout"
            partial = probes
        else:
            category = "decode-error"
            partial = probes
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
