#!/usr/bin/env python3

"""Run a bounded primary-voice call without starting MotionFixture."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
import uuid
from pathlib import Path

from run_screen_stream_smoke import run_smoke as run_screen_smoke


class VoiceSmokeError(RuntimeError):
    pass


class PrimaryVoiceObserver:
    def __init__(self) -> None:
        self.records: list[dict] = []

    def run_metadata(self) -> dict:
        return {"primary_voice_controls_required": True}

    def cleanup(self) -> None:
        return None

    def advance(self, elapsed_seconds, host_reader, viewer_reader) -> None:
        return None

    def validate(self, host_reader, viewer_reader, host_records, viewer_records) -> dict:
        for role, reader, records in (
            ("host", host_reader, host_records),
            ("viewer", viewer_reader, viewer_records),
        ):
            if not any(
                line.strip() == "SMOKE_STATUS primary-voice-controls-ack=1"
                for line in reader.lines
            ):
                raise VoiceSmokeError(f"{role}-voice-control-ack-missing")
            required = {
                "local_audio_level_milli",
                "voice_packets_lost",
                "voice_jitter_us",
                "voice_concealed_samples",
                "voice_total_samples_received",
            }
            if not any(required <= record.keys() for record in records):
                raise VoiceSmokeError(f"{role}-voice-quality-stats-missing")
        return {
            "primary_voice_controls_acknowledged": True,
            "primary_voice_quality_stats_available": True,
        }


def _positive_voice(summary: dict) -> bool:
    try:
        return all(
            int(summary[role][field]) > 0
            for role in ("host", "viewer")
            for field in ("voice_packets_sent", "voice_packets_received")
        )
    except (KeyError, TypeError, ValueError):
        return False


def _write_atomic_jsonl(path: Path, records: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8") as handle:
            for record in records:
                handle.write(
                    json.dumps(record, sort_keys=True, separators=(",", ":"))
                    + "\n"
                )
            handle.flush()
            os.fsync(handle.fileno())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def run_voice_smoke(
    *,
    demo: Path,
    server_root: Path,
    duration_seconds: int,
    port: int,
    artifact: Path,
) -> dict:
    if duration_seconds <= 0:
        raise VoiceSmokeError("duration-must-be-positive")
    if artifact.exists():
        raise VoiceSmokeError("refusing-to-overwrite-artifact")
    if not demo.is_file():
        raise VoiceSmokeError("demo-unavailable")

    with tempfile.TemporaryDirectory(prefix="shareme-primary-voice-") as directory:
        underlying = Path(directory) / "screen-smoke.jsonl"
        summary = run_screen_smoke(
            demo=demo,
            server_root=server_root,
            profile="standard",
            duration_seconds=duration_seconds,
            port=port,
            artifact=underlying,
            motion_fixture=None,
            role_environment_overrides={
                "host": {"SHAREME_PRIMARY_VOICE_SMOKE": "1"},
                "viewer": {"SHAREME_PRIMARY_VOICE_SMOKE": "1"},
            },
            scenario_observer=PrimaryVoiceObserver(),
        )
    if not _positive_voice(summary):
        raise VoiceSmokeError("bidirectional-primary-voice-missing")

    result = {
        "kind": "summary",
        "bidirectional_voice": True,
        "native_audio_playout": False,
        "motion_fixture_started": False,
        "primary_voice_controls_acknowledged": bool(
            summary.get("primary_voice_controls_acknowledged")
        ),
        "primary_voice_quality_stats_available": bool(
            summary.get("primary_voice_quality_stats_available")
        ),
        "host_voice_packets_sent": int(summary["host"]["voice_packets_sent"]),
        "host_voice_packets_received": int(
            summary["host"]["voice_packets_received"]
        ),
        "viewer_voice_packets_sent": int(
            summary["viewer"]["voice_packets_sent"]
        ),
        "viewer_voice_packets_received": int(
            summary["viewer"]["voice_packets_received"]
        ),
    }
    if not result["primary_voice_controls_acknowledged"]:
        raise VoiceSmokeError("primary-voice-controls-not-acknowledged")
    if not result["primary_voice_quality_stats_available"]:
        raise VoiceSmokeError("primary-voice-quality-stats-unavailable")
    _write_atomic_jsonl(
        artifact,
        [
            {
                "kind": "run",
                "version": 1,
                "run_id": uuid.uuid4().hex,
                "platform": os.sys.platform,
                "duration_seconds": duration_seconds,
                "demo_sha256": hashlib.sha256(demo.read_bytes()).hexdigest(),
                "motion_fixture_requested": False,
            },
            result,
        ],
    )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--server-root", type=Path, required=True)
    parser.add_argument("--duration-seconds", type=int, default=15)
    parser.add_argument("--port", type=int, default=18083)
    parser.add_argument("--artifact", type=Path, required=True)
    args = parser.parse_args()
    try:
        run_voice_smoke(
            demo=args.demo.resolve(),
            server_root=args.server_root.resolve(),
            duration_seconds=args.duration_seconds,
            port=args.port,
            artifact=args.artifact.resolve(),
        )
    except VoiceSmokeError as error:
        print(f"primary-voice-smoke-failed:{error}", file=os.sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
