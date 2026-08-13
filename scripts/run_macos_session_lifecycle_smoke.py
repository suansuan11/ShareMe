#!/usr/bin/env python3

"""Gate macOS call recovery around observed or controlled lifecycle events."""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_screen_stream_smoke as screen_smoke  # noqa: E402


EVENT_PATTERN = re.compile(
    r"^SMOKE_STATUS session-lifecycle-event "
    r"event=(will-sleep|did-wake|screen-locked|screen-unlocked) "
    r"generation=([1-9][0-9]*)$"
)
RECOVERED_PATTERN = re.compile(
    r"^SMOKE_STATUS session-lifecycle-recovered "
    r"generation=([1-9][0-9]*) decision=(healthy|capture-restarted)$"
)
ROOM_PATTERN = re.compile(r"\b[A-Z2-7]{6}\b")


class LifecycleSmokeError(screen_smoke.SmokeRuntimeError):
    pass


def validate_options(
    scenario: str,
    mode: str,
    duration_seconds: int,
    trigger_after_seconds: int,
) -> None:
    if sys.platform != "darwin":
        raise LifecycleSmokeError("session-lifecycle-smoke-is-macos-only")
    if scenario not in ("lock", "nested"):
        raise LifecycleSmokeError("invalid-lifecycle-scenario")
    if mode not in ("controlled", "physical-wait"):
        raise LifecycleSmokeError("invalid-lifecycle-mode")
    if duration_seconds < 30:
        raise LifecycleSmokeError("lifecycle-duration-too-short")
    if trigger_after_seconds < 5:
        raise LifecycleSmokeError("lifecycle-needs-warmup")
    final_event_offset = 4 if scenario == "nested" else 3
    if duration_seconds - trigger_after_seconds - final_event_offset < 10:
        raise LifecycleSmokeError("lifecycle-needs-post-resume-window")


def redact_lifecycle_failure(message: str) -> str:
    redacted = screen_smoke.redact_diagnostic(message)
    return ROOM_PATTERN.sub("[redacted]", redacted)


def _event_lines(reader) -> list[tuple[str, int]]:
    events: list[tuple[str, int]] = []
    for raw in reader.lines:
        match = EVENT_PATTERN.fullmatch(raw.strip())
        if match:
            events.append((match.group(1), int(match.group(2))))
    return events


def _recovery_sample(reader, role: str) -> int | None:
    counter_count = 0
    for raw in reader.lines:
        line = raw.strip()
        if line.startswith("PERF_COUNTERS ") and f"role={role}" in line.split():
            counter_count += 1
        if RECOVERED_PATTERN.fullmatch(line):
            return counter_count - 1 if counter_count else None
    return None


def _is_subsequence(required: tuple[str, ...], observed: list[str]) -> bool:
    position = 0
    for event in observed:
        if position < len(required) and event == required[position]:
            position += 1
    return position == len(required)


class LifecycleScenario:
    def __init__(
        self,
        *,
        scenario: str,
        mode: str,
        trigger_after_seconds: int,
        host_trigger_directory: Path | None = None,
        viewer_trigger_directory: Path | None = None,
    ):
        if scenario not in ("lock", "nested"):
            raise LifecycleSmokeError("invalid-lifecycle-scenario")
        if mode not in ("controlled", "physical-wait"):
            raise LifecycleSmokeError("invalid-lifecycle-mode")
        self.scenario = scenario
        self.mode = mode
        self.trigger_after_seconds = trigger_after_seconds
        self.records: list[dict] = []
        self._recorded: set[tuple[str, str, int]] = set()
        self._next_trigger = 0
        self._owned_directory: tempfile.TemporaryDirectory[str] | None = None

        if mode == "controlled" and (
            host_trigger_directory is None or viewer_trigger_directory is None
        ):
            self._owned_directory = tempfile.TemporaryDirectory(
                prefix="shareme-session-lifecycle-"
            )
            root = Path(self._owned_directory.name)
            host_trigger_directory = root / "host"
            viewer_trigger_directory = root / "viewer"
        self.host_trigger_directory = host_trigger_directory
        self.viewer_trigger_directory = viewer_trigger_directory
        for directory in (
            self.host_trigger_directory,
            self.viewer_trigger_directory,
        ):
            if directory is not None:
                directory.mkdir(parents=True, exist_ok=True)

    @property
    def required_events(self) -> tuple[str, ...]:
        if self.scenario == "nested":
            return (
                "screen-locked",
                "will-sleep",
                "did-wake",
                "screen-unlocked",
            )
        return ("screen-locked", "screen-unlocked")

    @property
    def trigger_offsets(self) -> tuple[int, ...]:
        if self.scenario == "nested":
            return (0, 1, 3, 4)
        return (0, 3)

    def run_metadata(self) -> dict:
        return {
            "lifecycle_scenario": self.scenario,
            "lifecycle_mode": self.mode,
            "lifecycle_trigger_after_seconds": self.trigger_after_seconds,
        }

    def environment_overrides(self) -> dict[str, dict[str, str]]:
        if self.mode != "controlled":
            return {}
        if self.host_trigger_directory is None or self.viewer_trigger_directory is None:
            raise LifecycleSmokeError("controlled-trigger-directory-missing")
        return {
            "host": {
                "SHAREME_SESSION_LIFECYCLE_TRIGGER_DIRECTORY": str(
                    self.host_trigger_directory
                )
            },
            "viewer": {
                "SHAREME_SESSION_LIFECYCLE_TRIGGER_DIRECTORY": str(
                    self.viewer_trigger_directory
                )
            },
        }

    def _observe(self, host_reader, viewer_reader) -> None:
        for role, reader in (("host", host_reader), ("viewer", viewer_reader)):
            for event, generation in _event_lines(reader):
                identity = (role, event, generation)
                if identity in self._recorded:
                    continue
                self._recorded.add(identity)
                self.records.append({
                    "kind": "session-lifecycle",
                    "role": role,
                    "event": event,
                    "generation": generation,
                })

    def _both_observed(self, event: str) -> bool:
        return all((role, event, 1) in self._recorded for role in ("host", "viewer"))

    def _write_trigger(self, sequence: int, event: str) -> None:
        for directory in (
            self.host_trigger_directory,
            self.viewer_trigger_directory,
        ):
            if directory is None:
                raise LifecycleSmokeError("controlled-trigger-directory-missing")
            destination = directory / f"{sequence:04d}-{event}.trigger"
            temporary = directory / f".{sequence:04d}-{event}.tmp"
            temporary.write_text("event\n", encoding="utf-8")
            temporary.replace(destination)

    def advance(self, elapsed_seconds: float, host_reader, viewer_reader) -> None:
        self._observe(host_reader, viewer_reader)
        if self.mode != "controlled" or self._next_trigger >= len(self.required_events):
            return
        if self._next_trigger > 0 and not self._both_observed(
            self.required_events[self._next_trigger - 1]
        ):
            return
        due = self.trigger_after_seconds + self.trigger_offsets[self._next_trigger]
        if elapsed_seconds < due:
            return
        event = self.required_events[self._next_trigger]
        self._write_trigger(self._next_trigger + 1, event)
        self._next_trigger += 1

    def validate(
        self,
        host_reader,
        viewer_reader,
        host_records: list[dict],
        viewer_records: list[dict],
    ) -> dict:
        self._observe(host_reader, viewer_reader)
        recovery_samples: dict[str, int] = {}
        for role, reader, records in (
            ("host", host_reader, host_records),
            ("viewer", viewer_reader, viewer_records),
        ):
            observed = [event for event, generation in _event_lines(reader)
                        if generation == 1]
            if not _is_subsequence(self.required_events, observed):
                raise LifecycleSmokeError(f"{role}-lifecycle-events-missing")
            recovery_sample = _recovery_sample(reader, role)
            if recovery_sample is None:
                raise LifecycleSmokeError(f"{role}-recovery-marker-missing")
            if len(records) - 1 - recovery_sample < 10:
                raise LifecycleSmokeError(f"{role}-post-resume-window-too-short")
            recovery_samples[role] = recovery_sample

            start = records[recovery_sample]
            final = records[-1]
            video_keys = (
                ("submitted", "encoded", "bytes_sent")
                if role == "host"
                else ("submitted", "received", "decoded", "bytes_received")
            )
            for key in (*video_keys, "voice_packets_sent", "voice_packets_received"):
                if not isinstance(start.get(key), int) or not isinstance(final.get(key), int):
                    raise LifecycleSmokeError(f"{role}-post-resume-counter-missing")
                if final[key] <= start[key]:
                    raise LifecycleSmokeError(f"{role}-post-resume-media-stalled")

        final_host = host_records[-1]
        restart_keys = (
            "screen_capture_restart_attempts",
            "screen_capture_restart_successes",
            "screen_capture_generation",
        )
        if any(final_host.get(key) != 0 for key in restart_keys):
            raise LifecycleSmokeError("healthy-session-restarted-capture")
        return {
            "lifecycle_verified": True,
            "lifecycle_mode": self.mode,
            "lifecycle_scenario": self.scenario,
            "healthy_call_preserved": True,
            "post_resume_samples": min(
                len(host_records) - recovery_samples["host"],
                len(viewer_records) - recovery_samples["viewer"],
            ) - 1,
        }

    def cleanup(self) -> None:
        if self._owned_directory is not None:
            self._owned_directory.cleanup()
            self._owned_directory = None


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path)
    parser.add_argument("--server-root", type=Path, default=repo_root / "server")
    parser.add_argument("--motion-fixture", type=Path, required=True)
    parser.add_argument("--profile", choices=tuple(screen_smoke.PROFILE_BOUNDS),
                        default="quality")
    parser.add_argument("--duration-seconds", type=int, default=60)
    parser.add_argument("--trigger-after-seconds", type=int, default=10)
    parser.add_argument("--scenario", choices=("lock", "nested"), required=True)
    parser.add_argument("--mode", choices=("controlled", "physical-wait"),
                        default="controlled")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--artifact", type=Path, required=True)
    args = parser.parse_args()

    scenario: LifecycleScenario | None = None
    try:
        validate_options(args.scenario, args.mode, args.duration_seconds,
                         args.trigger_after_seconds)
        scenario = LifecycleScenario(
            scenario=args.scenario,
            mode=args.mode,
            trigger_after_seconds=args.trigger_after_seconds,
        )
        if args.mode == "physical-wait":
            print(
                "LIFECYCLE_INSTRUCTION 请在画面稳定后手动完成所选系统事件；"
                "工具只等待系统通知，不会控制睡眠或锁屏。",
                flush=True,
            )
        demo = args.demo or screen_smoke._find_demo(repo_root)
        summary = screen_smoke.run_smoke(
            demo=demo.resolve(),
            server_root=args.server_root.resolve(),
            profile=args.profile,
            duration_seconds=args.duration_seconds,
            port=args.port,
            artifact=args.artifact.resolve(),
            screen_encoder="auto",
            motion_fixture=args.motion_fixture.resolve(),
            role_environment_overrides=scenario.environment_overrides(),
            scenario_observer=scenario,
        )
        print(json.dumps(summary, sort_keys=True))
        return 0
    except (LifecycleSmokeError, screen_smoke.SmokeRuntimeError,
            OSError, ValueError) as error:
        print(f"LIFECYCLE_SMOKE_ERROR {redact_lifecycle_failure(str(error))}",
              file=sys.stderr)
        return 1
    finally:
        if scenario is not None:
            scenario.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
