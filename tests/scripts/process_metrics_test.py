#!/usr/bin/env python3

import importlib.util
import os
import sys
import time
import unittest
from pathlib import Path
from unittest import mock


class ProcessMetricsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = Path(__file__).parents[2] / "scripts" / "process_metrics.py"
        spec = importlib.util.spec_from_file_location("process_metrics", script)
        cls.metrics = importlib.util.module_from_spec(spec)
        assert spec.loader
        sys.modules[spec.name] = cls.metrics
        spec.loader.exec_module(cls.metrics)

    def test_cpu_uses_elapsed_process_time(self):
        previous = self.metrics.RawProcessTimes(
            monotonic_ms=1_000,
            process_100ns=10_000_000,
        )
        current = self.metrics.RawProcessTimes(
            monotonic_ms=3_000,
            process_100ns=14_000_000,
        )
        self.assertAlmostEqual(
            self.metrics.cpu_percent(previous, current, logical_processors=4),
            5.0,
        )

    def test_rejects_regressing_samples(self):
        earlier = self.metrics.RawProcessTimes(
            monotonic_ms=1_000,
            process_100ns=500,
        )
        later = self.metrics.RawProcessTimes(
            monotonic_ms=2_000,
            process_100ns=700,
        )
        with self.assertRaisesRegex(
                self.metrics.ProcessMetricsError, "process-times-regressed"):
            self.metrics.cpu_percent(later, earlier, logical_processors=2)

    def test_summarizes_nonempty_cpu_and_rss_samples(self):
        samples = [
            self.metrics.ProcessSample(1_000, 2.5, 1_024),
            self.metrics.ProcessSample(1_250, 7.5, 2_048),
        ]
        self.assertEqual(self.metrics.summarize_samples(samples), {
            "sampleCount": 2,
            "cpuMeanPercent": 5.0,
            "cpuMaxPercent": 7.5,
            "rssMaxKiB": 2,
        })

    def test_samples_the_running_process_with_real_rss_evidence(self):
        sampler = self.metrics.ProcessSampler(os.getpid())
        time.sleep(0.02)
        sample = sampler.sample()
        self.assertGreater(sample.monotonic_ms, 0)
        self.assertGreater(sample.rss_bytes, 0)
        self.assertGreaterEqual(sample.cpu_percent, 0.0)

    def test_closes_backend_deterministically(self):
        class Backend:
            def __init__(self):
                self.closed = False

            def close(self):
                self.closed = True

        backend = Backend()
        with mock.patch.object(self.metrics, "create_process_backend",
                               return_value=backend):
            sampler = self.metrics.ProcessSampler(123)
            sampler.close()
        self.assertTrue(backend.closed)


if __name__ == "__main__":
    unittest.main()
