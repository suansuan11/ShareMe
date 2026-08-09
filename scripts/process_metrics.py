#!/usr/bin/env python3

import ctypes
import dataclasses
import os
import subprocess
import sys
import time


class ProcessMetricsError(RuntimeError):
    def __init__(self, category: str):
        super().__init__(category)
        self.category = category


@dataclasses.dataclass(frozen=True)
class RawProcessTimes:
    monotonic_ms: int
    process_100ns: int


@dataclasses.dataclass(frozen=True)
class ProcessSample:
    monotonic_ms: int
    cpu_percent: float
    rss_bytes: int


def cpu_percent(previous: RawProcessTimes, current: RawProcessTimes,
                logical_processors: int) -> float:
    # Per-machine convention: aggregate process time is divided by all logical
    # processors, so a fully busy one-core process is 100 / processor_count.
    elapsed_ms = current.monotonic_ms - previous.monotonic_ms
    process_100ns = current.process_100ns - previous.process_100ns
    if elapsed_ms <= 0 or process_100ns < 0:
        raise ProcessMetricsError("process-times-regressed")
    if logical_processors <= 0:
        raise ProcessMetricsError("process-samples-missing")
    return 100 * process_100ns / (elapsed_ms * 10_000 * logical_processors)


def summarize_samples(samples: list[ProcessSample]) -> dict:
    if not samples:
        raise ProcessMetricsError("process-samples-missing")
    cpu_samples = [sample.cpu_percent for sample in samples]
    rss_samples = [sample.rss_bytes for sample in samples]
    return {
        "sampleCount": len(samples),
        "cpuMeanPercent": round(sum(cpu_samples) / len(cpu_samples), 3),
        "cpuMaxPercent": round(max(cpu_samples), 3),
        "rssMaxKiB": max(rss_samples) // 1024,
    }


def _monotonic_ms() -> int:
    return round(time.monotonic() * 1000)


def _parse_process_time(value: str) -> int:
    try:
        days = 0
        clock = value.strip()
        if "-" in clock:
            days_text, clock = clock.split("-", 1)
            days = int(days_text)
        pieces = clock.split(":")
        if len(pieces) == 2:
            hours = 0
            minutes, seconds = pieces
        elif len(pieces) == 3:
            hours, minutes, seconds = pieces
        else:
            raise ValueError
        return int((days * 86_400 + int(hours) * 3_600 + int(minutes) * 60
                    + float(seconds)) * 10_000_000)
    except ValueError as error:
        raise ProcessMetricsError("process-output-malformed") from error


class _PsProcessBackend:
    def __init__(self, pid: int):
        self._pid = pid
        self._logical_processors = os.cpu_count() or 1
        self._previous = self._read_raw()

    def _read_raw(self) -> RawProcessTimes:
        result = subprocess.run(
            ["ps", "-o", "time=", "-o", "rss=", "-p", str(self._pid)],
            capture_output=True,
            text=True,
            check=False,
        )
        fields = result.stdout.split()
        if result.returncode != 0 or not fields:
            raise ProcessMetricsError("process-early-exit")
        if len(fields) != 2:
            raise ProcessMetricsError("process-output-malformed")
        try:
            self._rss_bytes = int(fields[1]) * 1024
        except ValueError as error:
            raise ProcessMetricsError("process-output-malformed") from error
        return RawProcessTimes(_monotonic_ms(), _parse_process_time(fields[0]))

    def sample(self) -> ProcessSample:
        current = self._read_raw()
        result = ProcessSample(
            current.monotonic_ms,
            cpu_percent(self._previous, current, self._logical_processors),
            self._rss_bytes,
        )
        self._previous = current
        return result


class _WindowsProcessBackend:
    _PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    _PROCESS_VM_READ = 0x0010
    _ERROR_ACCESS_DENIED = 5
    _ERROR_INVALID_PARAMETER = 87

    class _FileTime(ctypes.Structure):
        _fields_ = [("dwLowDateTime", ctypes.c_ulong),
                    ("dwHighDateTime", ctypes.c_ulong)]

    class _ProcessMemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("PageFaultCount", ctypes.c_ulong),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    def __init__(self, pid: int):
        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._psapi = ctypes.WinDLL("psapi", use_last_error=True)
        self._kernel32.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_bool,
                                               ctypes.c_ulong]
        self._kernel32.OpenProcess.restype = ctypes.c_void_p
        self._kernel32.GetProcessTimes.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(self._FileTime),
            ctypes.POINTER(self._FileTime), ctypes.POINTER(self._FileTime),
            ctypes.POINTER(self._FileTime),
        ]
        self._kernel32.GetProcessTimes.restype = ctypes.c_bool
        self._kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        self._kernel32.CloseHandle.restype = ctypes.c_bool
        self._psapi.GetProcessMemoryInfo.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(self._ProcessMemoryCounters),
            ctypes.c_ulong,
        ]
        self._psapi.GetProcessMemoryInfo.restype = ctypes.c_bool
        self._handle = self._kernel32.OpenProcess(
            self._PROCESS_QUERY_LIMITED_INFORMATION | self._PROCESS_VM_READ,
            False,
            pid,
        )
        if not self._handle:
            error = ctypes.get_last_error()
            raise ProcessMetricsError(self._error_category(error))
        self._logical_processors = os.cpu_count() or 1
        self._previous = self._read_raw()

    @staticmethod
    def _file_time_value(value: _FileTime) -> int:
        return (value.dwHighDateTime << 32) | value.dwLowDateTime

    @classmethod
    def _error_category(cls, error: int) -> str:
        if error == cls._ERROR_ACCESS_DENIED:
            return "process-access-denied"
        if error == cls._ERROR_INVALID_PARAMETER:
            return "process-early-exit"
        return "process-samples-missing"

    def _read_raw(self) -> RawProcessTimes:
        creation = self._FileTime()
        exit_time = self._FileTime()
        kernel = self._FileTime()
        user = self._FileTime()
        if not self._kernel32.GetProcessTimes(
                self._handle, ctypes.byref(creation), ctypes.byref(exit_time),
                ctypes.byref(kernel), ctypes.byref(user)):
            raise ProcessMetricsError(self._error_category(ctypes.get_last_error()))
        counters = self._ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if not self._psapi.GetProcessMemoryInfo(
                self._handle, ctypes.byref(counters), counters.cb):
            raise ProcessMetricsError(self._error_category(ctypes.get_last_error()))
        self._rss_bytes = counters.WorkingSetSize
        return RawProcessTimes(
            _monotonic_ms(),
            self._file_time_value(kernel) + self._file_time_value(user),
        )

    def sample(self) -> ProcessSample:
        current = self._read_raw()
        result = ProcessSample(
            current.monotonic_ms,
            cpu_percent(self._previous, current, self._logical_processors),
            self._rss_bytes,
        )
        self._previous = current
        return result

    def close(self) -> None:
        if self._handle:
            self._kernel32.CloseHandle(self._handle)
            self._handle = None

    def __del__(self) -> None:
        self.close()


def create_process_backend(pid: int, platform: str):
    if platform == "win32":
        return _WindowsProcessBackend(pid)
    if platform in ("darwin", "linux") or platform.startswith("linux"):
        return _PsProcessBackend(pid)
    raise ProcessMetricsError("process-samples-missing")


class ProcessSampler:
    def __init__(self, pid: int):
        self._backend = create_process_backend(pid, sys.platform)

    def sample(self) -> ProcessSample:
        return self._backend.sample()
