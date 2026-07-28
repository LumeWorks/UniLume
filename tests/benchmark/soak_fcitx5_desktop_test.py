#!/usr/bin/env python3
"""Unit tests for deterministic parts of the desktop soak harness."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
HARNESS_DIRECTORY = ROOT / "scripts" / "benchmark"
sys.path.insert(0, str(HARNESS_DIRECTORY))
HARNESS_PATH = HARNESS_DIRECTORY / "soak_fcitx5_desktop.py"
SPEC = importlib.util.spec_from_file_location("soak_fcitx5_desktop", HARNESS_PATH)
assert SPEC is not None and SPEC.loader is not None
HARNESS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = HARNESS
SPEC.loader.exec_module(HARNESS)


class GrowthDetectionTest(unittest.TestCase):
    def test_linear_resource_growth_requires_sustained_material_increase(
        self,
    ) -> None:
        self.assertTrue(HARNESS.linear_growth([100, 102, 104, 106, 108, 110], 1))
        self.assertFalse(
            HARNESS.linear_growth([100, 110, 100, 110, 100, 110], 1)
        )
        self.assertFalse(HARNESS.linear_growth([100, 100, 100, 100, 100], 0))

    def test_latency_growth_reports_drift_and_sustained_direction(self) -> None:
        drift, sustained = HARNESS.latency_growth(
            [100, 110, 120, 150, 160, 170]
        )
        self.assertAlmostEqual(drift, 45.454545, places=5)
        self.assertTrue(sustained)


class RestartTest(unittest.TestCase):
    def test_restart_detaches_daemon_output_and_observes_new_pid(self) -> None:
        completed = subprocess.CompletedProcess(["fcitx5", "-rd"], 0)
        with (
            mock.patch.object(
                HARNESS.subprocess, "run", return_value=completed
            ) as launch,
            mock.patch.object(HARNESS, "fcitx_pid", return_value=456),
            mock.patch.object(
                HARNESS,
                "run",
                return_value=subprocess.CompletedProcess(
                    ["fcitx5-remote", "-n"], 0, stdout="unilume\n"
                ),
            ),
        ):
            self.assertEqual(HARNESS.restart_fcitx(123, 2.0), 456)
        launch.assert_called_once_with(
            ["fcitx5", "-rd"],
            check=False,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=2.0,
        )


if __name__ == "__main__":
    unittest.main()
