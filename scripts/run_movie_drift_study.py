#!/usr/bin/env python3

"""Run exactly three sequential drift-study-v1 sessions and summarize JSONL."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from run_signaled_call_smoke import (  # noqa: E402
    popen_group_options,
    start_managed_process,
    start_signaling_server,
    terminate_process_group,
    wait_for_health,
)


ROOM_PATTERN = re.compile(r"^ROOM ([A-Z2-7]{6})$")
COMPLETE_RESULT = "RESULT drift-study-v1 status=complete"
FAILED_RESULT = re.compile(r"^RESULT drift-study-v1 status=failed(?: |$)")
PAUSE_PHASE = "paused"
STEADY_PHASES = {
    "steady",
    "post-resume",
    "post-forward-seek",
    "post-backward-seek",
}


class DriftStudyError(RuntimeError):
    pass


class OutputReader:
    def __init__(self, process: Any):
        self.process = process
        self.lines: list[str] = []
        self.events: queue.Queue[Optional[str]] = queue.Queue()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        stream = getattr(self.process, "stdout", None)
        if stream is None:
            self.events.put(None)
            return
        try:
            for line in stream:
                self.lines.append(line)
                self.events.put(line)
        finally:
            self.events.put(None)


def parse_room_line(line: str) -> Optional[str]:
    match = ROOM_PATTERN.fullmatch(line.strip())
    return match.group(1) if match else None


def build_host_command(
    demo: Path, server_url: str, movie: Path, output_path: Path
) -> list[str]:
    return [
        str(demo),
        "--server", server_url,
        "--role", "host",
        "--source", "movie",
        "--movie", str(movie),
        "--movie-audio",
        "--metrics-jsonl", str(output_path),
        "--drift-scenario", "drift-study-v1",
        "--measurement-duration-seconds", "300",
    ]


def build_viewer_command(
    demo: Path, server_url: str, room: str
) -> list[str]:
    return [
        str(demo),
        "--server", server_url,
        "--role", "viewer",
        "--room", room,
        "--source", "test",
    ]


def validate_run_count(run_count: int) -> int:
    if run_count != 3:
        raise ValueError("drift-study-v1 requires exactly three sequential runs")
    return run_count


def prepare_output_root(output_root: Path, output_parent: Path) -> Path:
    parent = output_parent.resolve()
    root = output_root.resolve()
    try:
        root.relative_to(parent)
    except ValueError as error:
        raise ValueError("output root must remain below the explicit output parent") from error
    root.mkdir(parents=True, exist_ok=True)
    return root


def refuse_existing_artifact(path: Path) -> None:
    if path.exists():
        raise FileExistsError(f"artifact already exists: {path.name}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def redact_diagnostic(text: str, secrets: list[str]) -> str:
    redacted = text
    for secret in sorted((value for value in secrets if value), key=len, reverse=True):
        redacted = redacted.replace(secret, "<redacted>")
    return redacted


def build_demo_environment(
    base: dict[str, str], qt_platform: Optional[str]
) -> dict[str, str]:
    environment = dict(base)
    if qt_platform:
        environment["QT_QPA_PLATFORM"] = qt_platform
    else:
        environment.pop("QT_QPA_PLATFORM", None)
    return environment


def viewer_is_alive(process: Any) -> bool:
    return process.poll() is None


def complete_result_requires_viewer(output: str, viewer: Any) -> bool:
    return result_is_complete(output) and viewer_is_alive(viewer)


def result_is_complete(output: str) -> bool:
    return any(line.strip().startswith(COMPLETE_RESULT) for line in output.splitlines())


def parse_result_counters(output: str) -> dict[str, int]:
    for line in output.splitlines():
        if not line.startswith(COMPLETE_RESULT):
            continue
        values = dict(re.findall(r"(accepted_samples|rejected_samples|received_reports)=(\d+)", line))
        return {
            "acceptedSamples": int(values.get("accepted_samples", 0)),
            "rejectedSamples": int(values.get("rejected_samples", 0)),
            "receivedReports": int(values.get("received_reports", 0)),
        }
    return {}


def is_complete_artifact(path: Path) -> bool:
    if not path.is_file():
        return False
    try:
        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    except (OSError, json.JSONDecodeError):
        return False
    summaries = [record for record in records if record.get("kind") == "summary"]
    return bool(summaries and summaries[-1].get("complete") is True)


def nearest_rank(values: list[int], percentile: int) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    rank = max(1, (len(ordered) * percentile + 99) // 100)
    return ordered[min(rank, len(ordered)) - 1]


def recompute_summary(samples: list[dict[str, Any]]) -> dict[str, Any]:
    deltas = [int(sample["deltaMs"]) for sample in samples]
    absolute = [abs(value) for value in deltas]
    phases: dict[str, int] = {}
    sequence_regressions = 0
    generation_regressions = 0
    largest_gap = 0
    report_gap_count = 0
    previous: Optional[dict[str, Any]] = None
    for sample in samples:
        phase = str(sample.get("phase", "unknown"))
        phases[phase] = phases.get(phase, 0) + 1
        if previous is not None:
            if int(sample["sampleIndex"]) <= int(previous["sampleIndex"]):
                sequence_regressions += 1
            if int(sample["reportSequence"]) <= int(previous["reportSequence"]):
                sequence_regressions += 1
            if int(sample["generation"]) < int(previous["generation"]):
                generation_regressions += 1
            gap = int(sample["captureTimeMs"]) - int(previous["captureTimeMs"])
            if gap > 250 and phase != PAUSE_PHASE and previous.get("phase") != PAUSE_PHASE:
                report_gap_count += 1
                largest_gap = max(largest_gap, gap)
        previous = sample

    steady = [
        abs(int(sample["deltaMs"]))
        for sample in samples
        if sample.get("phase") in STEADY_PHASES
    ]
    within_100 = sum(value <= 100 for value in steady)
    return {
        "acceptedSamples": len(samples),
        "phaseCounts": phases,
        "signedMinMs": min(deltas) if deltas else 0,
        "signedMaxMs": max(deltas) if deltas else 0,
        "signedMeanMs": sum(deltas) / len(deltas) if deltas else 0.0,
        "absoluteP50Ms": nearest_rank(absolute, 50),
        "absoluteP95Ms": nearest_rank(absolute, 95),
        "absoluteP99Ms": nearest_rank(absolute, 99),
        "absoluteMaxMs": max(absolute) if absolute else 0,
        "steadySamples": len(steady),
        "steadyWithin100Ms": within_100,
        "steadyWithin100Ratio": within_100 / len(steady) if steady else 0.0,
        "steadyAbsoluteP99Ms": nearest_rank(steady, 99),
        "sequenceRegressions": sequence_regressions,
        "generationRegressions": generation_regressions,
        "reportGapCount": report_gap_count,
        "largestReportGapMs": largest_gap,
    }


def read_artifact(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    samples = [record for record in records if record.get("kind") == "sample"]
    summaries = [record for record in records if record.get("kind") == "summary"]
    if not summaries:
        raise DriftStudyError("missing-summary")
    return samples, summaries[-1]


def summarize_artifact(path: Path) -> dict[str, Any]:
    samples, recorded = read_artifact(path)
    if recorded.get("complete") is not True:
        raise DriftStudyError("incomplete-artifact")
    recomputed = recompute_summary(samples)
    for key in (
        "acceptedSamples", "absoluteP50Ms", "absoluteP95Ms", "absoluteP99Ms",
        "absoluteMaxMs", "signedMinMs", "signedMaxMs", "reportGapCount",
        "largestReportGapMs",
    ):
        if recorded.get(key) != recomputed[key]:
            raise DriftStudyError(f"summary-mismatch-{key}")
    recomputed["recoveries"] = recorded.get("recoveries", [])
    recomputed["errors"] = recorded.get("errors", [])
    recomputed["acceptedOutsidePause"] = recomputed["acceptedSamples"] - recomputed["phaseCounts"].get(PAUSE_PHASE, 0)
    recomputed["artifact"] = path.name
    recomputed["sha256"] = sha256_file(path)
    return recomputed


def gates_pass(report: dict[str, Any]) -> bool:
    runs = report.get("runs", [])
    if len(runs) != 3 or any(run.get("acceptedOutsidePause", 0) < 900 for run in runs):
        return False
    if any(
        run.get("sequenceRegressions", 0) != 0
        or run.get("generationRegressions", 0) != 0
        or run.get("largestReportGapMs", 0) > 2_000
        or run.get("steadyAbsoluteP99Ms", 0) >= 300
        or run.get("steadyWithin100Ratio", 0.0) < 0.95
        or run.get("errors")
        for run in runs
    ):
        return False
    required_recoveries = {
        "post-resume", "post-forward-seek", "post-backward-seek"
    }
    for run in runs:
        completed_recoveries = {
            recovery.get("phase")
            for recovery in run.get("recoveries", [])
            if recovery.get("complete")
        }
        if not required_recoveries.issubset(completed_recoveries):
            return False
        for recovery in run.get("recoveries", []):
            if not recovery.get("complete") or int(recovery.get("durationMs", 0)) > 5_000:
                return False
    return True


def _wait_for_event(reader: OutputReader, timeout: float) -> Optional[str]:
    try:
        return reader.events.get(timeout=max(0.0, timeout))
    except queue.Empty as error:
        raise DriftStudyError("process-timeout") from error


def _start_demo(command: list[str], environment: dict[str, str]) -> tuple[Any, OutputReader]:
    try:
        process = start_managed_process(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
            **popen_group_options(),
        )
    except OSError as error:
        raise DriftStudyError("process-start-failed") from error
    return process, OutputReader(process)


def run_one(
    demo: Path,
    server_url: str,
    movie: Path,
    output_path: Path,
    timeout_seconds: float,
    environment: Optional[dict[str, str]] = None,
) -> dict[str, Any]:
    refuse_existing_artifact(output_path)
    env = os.environ.copy()
    if environment:
        env.update(environment)
    host = viewer = None
    host_reader = viewer_reader = None
    try:
        host, host_reader = _start_demo(
            build_host_command(demo, server_url, movie, output_path), env
        )
        deadline = time.monotonic() + min(30.0, timeout_seconds)
        room = None
        while room is None:
            event = _wait_for_event(host_reader, deadline - time.monotonic())
            if event is None:
                raise DriftStudyError("room-not-created")
            room = parse_room_line(event)
            if host.poll() is not None and room is None:
                raise DriftStudyError("host-exited-before-room")
        viewer, viewer_reader = _start_demo(
            build_viewer_command(demo, server_url, room), env
        )
        result_lines: list[str] = []
        deadline = time.monotonic() + timeout_seconds
        while True:
            if not viewer_is_alive(viewer):
                raise DriftStudyError("viewer-exited-before-result")
            event = _wait_for_event(host_reader, deadline - time.monotonic())
            if event is None:
                raise DriftStudyError("host-exited-without-result")
            result_lines.append(event)
            if event.strip().startswith("RESULT drift-study-v1"):
                break
        output = "".join(result_lines + host_reader.lines[len(result_lines):])
        if not complete_result_requires_viewer(output, viewer):
            if not viewer_is_alive(viewer):
                raise DriftStudyError("viewer-exited-before-result")
            raise DriftStudyError("measurement-failed")
        host.wait(timeout=10)
        if not viewer_is_alive(viewer):
            raise DriftStudyError("viewer-exited-after-result")
        if not is_complete_artifact(output_path):
            raise DriftStudyError("incomplete-artifact")
        report = summarize_artifact(output_path)
        report.update(parse_result_counters(output))
        return report
    except (DriftStudyError, OSError, TimeoutError):
        if output_path.exists():
            return {"artifact": output_path.name, "complete": False}
        raise
    finally:
        for process in (viewer, host):
            if process is not None and process.poll() is None:
                terminate_process_group(process, grace_seconds=1)


def combine_reports(run_reports: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "runs": run_reports,
        "gatePassed": gates_pass({"runs": run_reports}),
    }


def run_study(args: argparse.Namespace) -> dict[str, Any]:
    validate_run_count(args.run_count)
    root = prepare_output_root(args.output_root, args.output_parent)
    if not args.demo.is_file():
        raise DriftStudyError("demo-not-found")
    if not args.movie.is_file():
        raise DriftStudyError("movie-not-found")

    server = server_log = server_directory = None
    if args.server_root is not None:
        from urllib.parse import urlparse
        parsed = urlparse(args.server_url)
        if parsed.port is None:
            raise DriftStudyError("server-port-required")
        server, server_log, server_directory = start_signaling_server(
            args.server_root, parsed.hostname or "127.0.0.1", parsed.port
        )
        wait_for_health(
            f"http://{parsed.hostname or '127.0.0.1'}:{parsed.port}/healthz",
            server,
            server_log=server_log,
        )

    reports: list[dict[str, Any]] = []
    try:
        for index in range(1, 4):
            output_path = root / f"run-{index:02d}.jsonl"
            report = run_one(
                args.demo, args.server_url, args.movie, output_path,
                args.timeout_seconds,
                build_demo_environment(os.environ.copy(), args.qt_platform),
            )
            if not report.get("acceptedSamples"):
                raise DriftStudyError(
                    "run-not-complete-accepted-{}-received-{}".format(
                        report.get("acceptedSamples", 0),
                        report.get("receivedReports", 0),
                    )
                )
            reports.append(report)
        result = combine_reports(reports)
        summary_path = root / "combined-summary.json"
        refuse_existing_artifact(summary_path)
        summary_path.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        return result
    finally:
        if server is not None and server.poll() is None:
            terminate_process_group(server, grace_seconds=1)
        if server_log is not None:
            server_log.close()
        if server_directory is not None:
            server_directory.cleanup()


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--server-url", required=True)
    parser.add_argument("--server-root", type=Path)
    parser.add_argument("--movie", type=Path, required=True)
    parser.add_argument("--output-parent", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--run-count", type=int, default=3)
    parser.add_argument("--timeout-seconds", type=float, default=390.0)
    parser.add_argument("--qt-platform")
    args = parser.parse_args(argv)
    try:
        result = run_study(args)
    except (DriftStudyError, OSError, ValueError) as error:
        print(f"DRIFT_STUDY_ERROR {error}", file=sys.stderr)
        return 1
    print(f"DRIFT_STUDY_RESULT gate_passed={str(result['gatePassed']).lower()}")
    return 0 if result["gatePassed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
