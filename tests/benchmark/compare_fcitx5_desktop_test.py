#!/usr/bin/env python3
"""Unit tests for deterministic parts of the real-desktop comparison harness."""

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
import urllib.request
from pathlib import Path
from unittest import mock


HARNESS_PATH = (
    Path(__file__).resolve().parents[2]
    / "scripts"
    / "benchmark"
    / "compare_fcitx5_desktop.py"
)
SPEC = importlib.util.spec_from_file_location("compare_fcitx5_desktop", HARNESS_PATH)
assert SPEC is not None and SPEC.loader is not None
HARNESS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = HARNESS
SPEC.loader.exec_module(HARNESS)


class ProbeWindowTitleTest(unittest.TestCase):
    def test_browser_suffix_is_captured(self) -> None:
        suffix = HARNESS.probe_window_suffix(
            "token|RESULT| - Google Chrome", "token"
        )
        self.assertEqual(suffix, " - Google Chrome")

    def test_preflight_rejects_non_probe_window(self) -> None:
        with self.assertRaises(HARNESS.HarnessError):
            HARNESS.probe_window_suffix("unrelated window", "token")


class ProbeReportStateTest(unittest.TestCase):
    def test_wait_returns_exact_application_value(self) -> None:
        reports = HARNESS.ProbeReportState()
        reports.record("token", 1, "tiếng Việt")
        self.assertEqual(reports.wait_for("token", "tiếng Việt", 0), "tiếng Việt")
        self.assertEqual(reports.wait_for("token", "different", 0), "tiếng Việt")
        self.assertIsNone(reports.wait_for("missing", "", 0))

    def test_late_older_report_cannot_overwrite_latest_value(self) -> None:
        reports = HARNESS.ProbeReportState()
        reports.record("token", 2, "latest")
        reports.record("token", 1, "stale")
        self.assertEqual(reports.wait_for("token", "latest", 0), "latest")

    def test_loopback_server_receives_real_probe_report(self) -> None:
        with HARNESS.ProbeReportServer(0) as reports:
            port = reports.server.server_address[1]
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/report",
                data=json.dumps(
                    {
                        "token": "token",
                        "revision": 1,
                        "value": "tiếng Việt",
                    }
                ).encode(),
                headers={"Content-Type": "text/plain"},
                method="POST",
            )
            with urllib.request.urlopen(request) as response:
                self.assertEqual(response.status, 204)
            self.assertEqual(
                reports.state.wait_for("token", "tiếng Việt", 1), "tiếng Việt"
            )


class InputInjectionTest(unittest.TestCase):
    def test_emit_input_reuses_xtest_connection(self) -> None:
        injector = mock.Mock()
        with (
            mock.patch.object(HARNESS.time, "sleep") as sleep,
            mock.patch.object(HARNESS.time, "monotonic_ns", return_value=123),
        ):
            self.assertEqual(HARNESS.emit_input(injector, "ab<BS>", 3), (3, 123))
        self.assertEqual(
            injector.method_calls,
            [
                mock.call.ascii("a"),
                mock.call.ascii("b"),
                mock.call.key("BackSpace"),
            ],
        )
        self.assertEqual(
            sleep.call_args_list,
            [mock.call(0.003), mock.call(0.003)],
        )


class SloEvaluationTest(unittest.TestCase):
    def test_latency_gate_compares_candidate_and_reference_percentiles(self) -> None:
        summary = {
            "errors": 0,
            "completion_ns": {"p50": 90, "p95": 90, "p99": 90},
            "keys_per_second": 110,
            "fcitx_cpu_seconds": 1.0,
            "fcitx_rss_delta_kib": {"max": 0},
        }
        result = {
            "candidate": {"summary": summary},
            "reference": {
                "summary": {
                    **summary,
                    "completion_ns": {"p50": 100, "p95": 100, "p99": 100},
                    "keys_per_second": 100,
                }
            },
            # Pair ratios remain useful dispersion evidence, but their own
            # percentiles are not latency distribution percentiles.
            "paired_summary": {
                "candidate_over_reference_completion_ratio": {
                    "p50": 2.0,
                    "p95": 3.0,
                    "p99": 4.0,
                }
            },
        }
        gate = HARNESS.evaluate_slo(
            result, cpu_noise_seconds=0.05, rss_noise_kib=64
        )
        self.assertTrue(gate["overall_pass"])


if __name__ == "__main__":
    unittest.main()
