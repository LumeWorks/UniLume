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
        reports.record("token", "tiếng Việt")
        self.assertEqual(reports.wait_for("token", "tiếng Việt", 0), "tiếng Việt")
        self.assertEqual(reports.wait_for("token", "different", 0), "tiếng Việt")
        self.assertIsNone(reports.wait_for("missing", "", 0))

    def test_loopback_server_receives_real_probe_report(self) -> None:
        with HARNESS.ProbeReportServer(0) as reports:
            port = reports.server.server_address[1]
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/report",
                data=json.dumps({"token": "token", "value": "tiếng Việt"}).encode(),
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


if __name__ == "__main__":
    unittest.main()
