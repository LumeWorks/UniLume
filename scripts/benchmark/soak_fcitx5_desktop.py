#!/usr/bin/env python3
"""Run a correctness and resource-bound Fcitx5 desktop soak.

The qualifying mode requires at least eight hours. A short smoke is available
only to validate the harness and is explicitly marked non-qualifying.
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

from compare_fcitx5_desktop import (
    HarnessError,
    PROBE_MARKER,
    active_input_method,
    emit_input,
    fcitx_pid,
    load_corpus,
    percentile,
    preflight,
    run,
    switch_input_method,
    wait_for_title,
    warm_input_path,
    xdotool,
)


def process_snapshot(pid: int) -> dict[str, int]:
    root = Path(f"/proc/{pid}")
    stat_fields = (root / "stat").read_text(encoding="utf-8").split()
    status = (root / "status").read_text(encoding="utf-8")
    values = {
        "cpu_ticks": int(stat_fields[13]) + int(stat_fields[14]),
        "rss_kib": 0,
        "peak_rss_kib": 0,
        "threads": 0,
        "file_descriptors": sum(1 for _ in (root / "fd").iterdir()),
    }
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            values["rss_kib"] = int(line.split()[1])
        elif line.startswith("VmHWM:"):
            values["peak_rss_kib"] = int(line.split()[1])
        elif line.startswith("Threads:"):
            values["threads"] = int(line.split()[1])
    if not values["rss_kib"] or not values["threads"]:
        raise HarnessError(f"incomplete Fcitx5 resource snapshot for PID {pid}")
    return values


def linear_growth(values: list[int], material: int) -> bool:
    if len(values) < 6 or values[-1] <= values[0] + material:
        return False
    increases = sum(
        current > previous
        for previous, current in zip(values, values[1:])
    )
    return increases * 5 >= (len(values) - 1) * 4


def latency_growth(values: list[int]) -> tuple[float, bool]:
    if len(values) < 2:
        return 0.0, False
    midpoint = len(values) // 2
    first = statistics.fmean(values[:midpoint])
    second = statistics.fmean(values[midpoint:])
    drift = 0.0 if first == 0 else (second - first) / first * 100.0
    increases = sum(
        current > previous
        for previous, current in zip(values, values[1:])
    )
    sustained = (
        len(values) >= 6
        and drift > 25.0
        and increases * 5 >= (len(values) - 1) * 4
    )
    return drift, sustained


def restart_fcitx(previous_pid: int, timeout_seconds: float) -> int:
    completed = run(["fcitx5", "-rd"], check=False)
    if completed.returncode != 0:
        raise HarnessError(
            f"Fcitx5 restart failed: {completed.stderr.strip()}"
        )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            pid = fcitx_pid()
            if pid != previous_pid:
                return pid
        except (HarnessError, ValueError):
            pass
        time.sleep(0.1)
    raise HarnessError("Fcitx5 did not restart before timeout")


def run_soak(arguments: argparse.Namespace) -> dict[str, object]:
    environment = preflight(arguments)
    scenarios = load_corpus(
        arguments.corpus,
        arguments.method,
        frozenset(arguments.scenario),
    )
    original_input_method = str(environment["active_input_method"])
    qualifying = arguments.smoke_seconds is None
    duration_seconds = (
        arguments.duration_hours * 3600.0
        if qualifying
        else arguments.smoke_seconds
    )
    deadline = time.monotonic() + duration_seconds
    next_restart = (
        time.monotonic() + arguments.restart_interval_hours * 3600.0
        if arguments.restart_interval_hours > 0
        else float("inf")
    )
    ticks_per_second = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    pid = fcitx_pid()
    started_wall = time.time()
    checkpoints: list[dict[str, object]] = []
    latencies: list[int] = []
    errors = 0
    key_events = 0
    cycles = 0
    restarts = 0

    try:
        switch_input_method(arguments.candidate, arguments.timeout_seconds)
        warm_input_path(
            arguments.window_id,
            arguments.token,
            arguments.delay_milliseconds,
            arguments.reset_settle_milliseconds,
            arguments.timeout_seconds,
        )
        initial = process_snapshot(pid)
        while time.monotonic() < deadline:
            for scenario in scenarios:
                if time.monotonic() >= deadline:
                    break
                xdotool("windowactivate", "--sync", arguments.window_id)
                xdotool(
                    "key",
                    "--window",
                    arguments.window_id,
                    "--clearmodifiers",
                    "Escape",
                )
                time.sleep(arguments.reset_settle_milliseconds / 1000.0)
                xdotool(
                    "key",
                    "--window",
                    arguments.window_id,
                    "--clearmodifiers",
                    "ctrl+a",
                )
                xdotool(
                    "key",
                    "--window",
                    arguments.window_id,
                    "--clearmodifiers",
                    "BackSpace",
                )
                cleared = f"{arguments.token}{PROBE_MARKER}"
                if (
                    wait_for_title(
                        arguments.window_id,
                        cleared,
                        arguments.timeout_seconds,
                    )
                    != cleared
                ):
                    raise HarnessError(
                        f"probe did not clear before {scenario.name}"
                    )
                before = process_snapshot(pid)
                started = time.monotonic_ns()
                key_events += emit_input(
                    arguments.window_id,
                    scenario.encoded_input,
                    arguments.delay_milliseconds,
                )
                expected = f"{arguments.token}{PROBE_MARKER}{scenario.expected}"
                correct = (
                    wait_for_title(
                        arguments.window_id,
                        expected,
                        arguments.timeout_seconds,
                    )
                    == expected
                )
                latency = time.monotonic_ns() - started
                after = process_snapshot(pid)
                latencies.append(latency)
                errors += not correct

                idle_before = after
                time.sleep(arguments.idle_seconds)
                idle_after = process_snapshot(pid)
                idle_cpu = (
                    idle_after["cpu_ticks"] - idle_before["cpu_ticks"]
                ) / ticks_per_second
                idle_utilization = (
                    idle_cpu / arguments.idle_seconds * 100.0
                    if arguments.idle_seconds
                    else 0.0
                )
                checkpoints.append(
                    {
                        "elapsed_seconds": time.time() - started_wall,
                        "cycle": cycles,
                        "scenario": scenario.name,
                        "correct": correct,
                        "completion_ns": latency,
                        "rss_kib": after["rss_kib"],
                        "peak_rss_kib": after["peak_rss_kib"],
                        "file_descriptors": after["file_descriptors"],
                        "threads": after["threads"],
                        "active_cpu_seconds": (
                            after["cpu_ticks"] - before["cpu_ticks"]
                        ) / ticks_per_second,
                        "idle_cpu_utilization_percent": idle_utilization,
                    }
                )
            cycles += 1
            if time.monotonic() >= next_restart:
                pid = restart_fcitx(pid, arguments.timeout_seconds)
                restarts += 1
                switch_input_method(
                    arguments.candidate,
                    arguments.timeout_seconds,
                )
                warm_input_path(
                    arguments.window_id,
                    arguments.token,
                    arguments.delay_milliseconds,
                    arguments.reset_settle_milliseconds,
                    arguments.timeout_seconds,
                )
                next_restart += arguments.restart_interval_hours * 3600.0
    finally:
        if original_input_method:
            restored = run(
                ["fcitx5-remote", "-s", original_input_method],
                check=False,
            )
            if restored.returncode != 0:
                print(
                    "warning: could not restore the original input method",
                    file=sys.stderr,
                )

    if not checkpoints:
        raise HarnessError("soak ended without a completed observation")
    elapsed_seconds = time.time() - started_wall
    rss = [int(point["rss_kib"]) for point in checkpoints]
    fds = [int(point["file_descriptors"]) for point in checkpoints]
    threads = [int(point["threads"]) for point in checkpoints]
    idle_cpu = [
        float(point["idle_cpu_utilization_percent"])
        for point in checkpoints
    ]
    latency_drift, latency_is_growing = latency_growth(latencies)
    checks = {
        "zero_output_errors": errors == 0,
        "rss_not_linear": not linear_growth(rss, 1024),
        "file_descriptors_not_linear": not linear_growth(fds, 0),
        "threads_not_linear": not linear_growth(threads, 0),
        "p99_latency_not_growing": not latency_is_growing,
        "idle_cpu_p95_within_budget":
            percentile(idle_cpu, 0.95)
            <= arguments.idle_cpu_budget_percent,
    }
    return {
        "schema_version": 1,
        "harness": "soak_fcitx5_desktop.py",
        "qualifying": qualifying,
        "environment": environment,
        "duration_seconds": elapsed_seconds,
        "cycles": cycles,
        "key_events": key_events,
        "errors": errors,
        "fcitx_restarts": restarts,
        "initial_resources": initial,
        "summary": {
            "completion_ns": {
                "p50": percentile(latencies, 0.50),
                "p95": percentile(latencies, 0.95),
                "p99": percentile(latencies, 0.99),
                "drift_percent": latency_drift,
                "linear_growth_detected": latency_is_growing,
            },
            "rss_kib": {"initial": rss[0], "final": rss[-1], "max": max(rss)},
            "file_descriptors": {
                "initial": fds[0],
                "final": fds[-1],
                "max": max(fds),
            },
            "threads": {
                "initial": threads[0],
                "final": threads[-1],
                "max": max(threads),
            },
            "idle_cpu_utilization_percent": {
                "p50": percentile(idle_cpu, 0.50),
                "p95": percentile(idle_cpu, 0.95),
                "max": max(idle_cpu),
            },
        },
        "gate": {
            "checks": checks,
            "functional_pass": all(checks.values()),
            "qualifying_duration_pass":
                qualifying and elapsed_seconds >= 8 * 3600,
            "overall_pass":
                qualifying
                and elapsed_seconds >= 8 * 3600
                and all(checks.values()),
        },
        "checkpoints": checkpoints,
    }


def parser() -> argparse.ArgumentParser:
    root = Path(__file__).resolve().parents[2]
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--candidate", required=True)
    # preflight() expects a reference field even though a soak has one side.
    result.add_argument("--reference", default="unused")
    result.add_argument("--window-id", required=True)
    result.add_argument("--token", required=True)
    result.add_argument(
        "--method",
        default="telex",
        choices=("telex", "vni", "viqr"),
    )
    result.add_argument("--scenario", action="append", default=[])
    result.add_argument(
        "--corpus",
        type=Path,
        default=root / "benchmarks/comparison/fcitx5-e2e.tsv",
    )
    result.add_argument("--duration-hours", type=float, default=8.0)
    result.add_argument(
        "--smoke-seconds",
        type=float,
        help="short non-qualifying harness check",
    )
    result.add_argument("--restart-interval-hours", type=float, default=2.0)
    result.add_argument("--idle-seconds", type=float, default=1.0)
    result.add_argument("--idle-cpu-budget-percent", type=float, default=5.0)
    result.add_argument("--delay-milliseconds", type=int, default=1)
    result.add_argument("--switch-settle-milliseconds", type=int, default=100)
    result.add_argument("--reset-settle-milliseconds", type=int, default=50)
    result.add_argument("--timeout-seconds", type=float, default=5.0)
    result.add_argument("--output", type=Path, required=True)
    return result


def main() -> int:
    arguments = parser().parse_args()
    if arguments.smoke_seconds is None and arguments.duration_hours < 8:
        raise HarnessError("a qualifying soak must run for at least eight hours")
    if (
        arguments.duration_hours <= 0
        or (
            arguments.smoke_seconds is not None
            and arguments.smoke_seconds <= 0
        )
        or arguments.restart_interval_hours < 0
        or arguments.idle_seconds <= 0
        or arguments.idle_cpu_budget_percent < 0
    ):
        raise HarnessError("durations and resource budgets are invalid")
    result = run_soak(arguments)
    arguments.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    required_gate = (
        result["gate"]["overall_pass"]
        if result["qualifying"]
        else result["gate"]["functional_pass"]
    )
    return 0 if required_gate else 3


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (HarnessError, OSError, subprocess.SubprocessError) as error:
        print(f"desktop soak error: {error}", file=sys.stderr)
        raise SystemExit(2)
