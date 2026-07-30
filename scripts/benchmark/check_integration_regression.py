#!/usr/bin/env python3
"""Run paired UniLume integration benchmarks and enforce relative budgets."""

from __future__ import annotations

import argparse
import json
import random
import statistics
import subprocess
import sys
from pathlib import Path


class RegressionError(RuntimeError):
    pass


def run(executable: Path, keys: int) -> dict[str, object]:
    # The binary exits non-zero when result.errors != 0. That is still a valid
    # JSON sample for regression comparison (e.g. a broken main baseline while
    # the candidate fixes correctness). Only fail hard when JSON is unusable.
    completed = subprocess.run(
        [
            str(executable),
            "--profile=immediate",
            f"--keys={keys}",
            "--format=json",
        ],
        check=False,
        text=True,
        capture_output=True,
        timeout=120,
    )
    if not completed.stdout.strip():
        detail = (completed.stderr or "").strip() or f"exit {completed.returncode}"
        raise RegressionError(f"benchmark produced no JSON output: {detail}")
    try:
        report = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RegressionError(
            f"benchmark JSON was unreadable (exit {completed.returncode}): {error}"
        ) from error
    rows = report.get("results", [])
    if len(rows) != 1 or rows[0].get("name") != "immediate-typing-disabled":
        raise RegressionError("benchmark did not return the immediate profile")
    rows[0]["metadata"] = report.get("metadata", {})
    rows[0]["process_exit_code"] = completed.returncode
    return rows[0]


def median(rows: list[dict[str, object]], field: str) -> float:
    return statistics.median(float(row[field]) for row in rows)


def latency_median(rows: list[dict[str, object]], field: str) -> float:
    return statistics.median(
        float(row["latency"][field])  # type: ignore[index]
        for row in rows
    )


def relative_mad(values: list[float]) -> float:
    center = statistics.median(values)
    if center == 0:
        return 0.0
    return statistics.median(abs(value - center) for value in values) / center


def evaluate(
    baseline: list[dict[str, object]],
    candidate: list[dict[str, object]],
    minimum_budget: float,
    maximum_budget: float,
) -> dict[str, object]:
    noises = []
    for rows in (baseline, candidate):
        noises.append(relative_mad([float(row["keys_per_second"]) for row in rows]))
        for percentile in ("p95_ns", "p99_ns"):
            noises.append(
                relative_mad(
                    [
                        float(row["latency"][percentile])  # type: ignore[index]
                        for row in rows
                    ]
                )
            )
    measured_noise = max(noises)
    allowed_regression = min(
        maximum_budget,
        max(minimum_budget, measured_noise * 3.0),
    )

    baseline_throughput = median(baseline, "keys_per_second")
    candidate_throughput = median(candidate, "keys_per_second")
    baseline_p95 = latency_median(baseline, "p95_ns")
    candidate_p95 = latency_median(candidate, "p95_ns")
    baseline_p99 = latency_median(baseline, "p99_ns")
    candidate_p99 = latency_median(candidate, "p99_ns")
    baseline_peak = max(
        int(row["rss"]["peak_kib"])  # type: ignore[index]
        for row in baseline
    )
    candidate_peak = max(
        int(row["rss"]["peak_kib"])  # type: ignore[index]
        for row in candidate
    )

    def row_correct(row: dict[str, object]) -> bool:
        return (
            int(row["errors"]) == 0
            and int(row["lost_events"]) == 0
            and int(row["duplicate_events"]) == 0
            and int(row["reordered_events"]) == 0
            and not bool(row["pending_transaction"])
            and not bool(row["rss"]["linear_growth_detected"])  # type: ignore[index]
        )

    candidate_correct = all(row_correct(row) for row in candidate)
    baseline_correct = all(row_correct(row) for row in baseline)
    candidate_checksums = {int(row["checksum"]) for row in candidate}
    candidate_key_counts = {int(row["total_keys"]) for row in candidate}
    # When baseline itself is correctness-broken (common while landing a fix),
    # require the candidate to be clean and internally consistent. When both
    # sides are clean, still require matching checksums across the pair.
    if baseline_correct and candidate_correct:
        checksums = {int(row["checksum"]) for row in baseline + candidate}
        key_counts = {int(row["total_keys"]) for row in baseline + candidate}
        correctness = len(checksums) == 1 and len(key_counts) == 1
    else:
        correctness = (
            candidate_correct
            and len(candidate_checksums) == 1
            and len(candidate_key_counts) == 1
        )
    checks = {
        "correctness_and_lifecycle": correctness,
        "throughput":
            candidate_throughput
            >= baseline_throughput * (1.0 - allowed_regression),
        "p95_latency":
            candidate_p95 <= baseline_p95 * (1.0 + allowed_regression),
        "p99_latency":
            candidate_p99 <= baseline_p99 * (1.0 + allowed_regression),
        "peak_rss": candidate_peak <= baseline_peak + 1024,
    }
    return {
        "noise": {
            "relative_mad_max": measured_noise,
            "allowed_regression_fraction": allowed_regression,
            "minimum_budget_fraction": minimum_budget,
            "maximum_budget_fraction": maximum_budget,
        },
        "medians": {
            "baseline": {
                "keys_per_second": baseline_throughput,
                "p95_ns": baseline_p95,
                "p99_ns": baseline_p99,
                "peak_rss_kib": baseline_peak,
            },
            "candidate": {
                "keys_per_second": candidate_throughput,
                "p95_ns": candidate_p95,
                "p99_ns": candidate_p99,
                "peak_rss_kib": candidate_peak,
            },
        },
        "checks": checks,
        "overall_pass": all(checks.values()),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--keys", type=int, default=1_000_000)
    parser.add_argument("--seed", type=int, default=55)
    parser.add_argument("--minimum-budget", type=float, default=0.10)
    parser.add_argument("--maximum-budget", type=float, default=0.15)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.rounds < 5 or arguments.keys < 10_000:
        raise RegressionError("at least five rounds and 10000 keys are required")
    if not (
        0 <= arguments.minimum_budget <= arguments.maximum_budget < 1
    ):
        raise RegressionError("invalid regression budget")

    rows: dict[str, list[dict[str, object]]] = {
        "baseline": [],
        "candidate": [],
    }
    schedule = []
    randomizer = random.Random(arguments.seed)
    for round_index in range(arguments.rounds):
        pair = ["baseline", "candidate"]
        randomizer.shuffle(pair)
        schedule.extend((round_index, side) for side in pair)
    for order, (round_index, side) in enumerate(schedule):
        executable = (
            arguments.baseline if side == "baseline" else arguments.candidate
        )
        row = run(executable.resolve(), arguments.keys)
        row["round"] = round_index
        row["order"] = order
        rows[side].append(row)

    result = {
        "schema_version": 1,
        "rounds": arguments.rounds,
        "keys": arguments.keys,
        "seed": arguments.seed,
        "evaluation": evaluate(
            rows["baseline"],
            rows["candidate"],
            arguments.minimum_budget,
            arguments.maximum_budget,
        ),
        "samples": rows,
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded, encoding="utf-8")
    else:
        sys.stdout.write(encoded)
    return 0 if result["evaluation"]["overall_pass"] else 3


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        RegressionError,
        OSError,
        subprocess.CalledProcessError,
        subprocess.TimeoutExpired,
        json.JSONDecodeError,
    ) as error:
        print(f"integration regression gate error: {error}", file=sys.stderr)
        raise SystemExit(2)
