#!/usr/bin/env python3
"""Record a fair, X11-only Fcitx5 input-method comparison.

This is a test harness, not a production input path. It deliberately requires
an operator to open a disposable browser profile with the supplied probe page
and to configure both input methods in Fcitx5 before it is run.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import hashlib
import http.server
import json
import math
import os
import random
import shutil
import statistics
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PROBE_MARKER = "|RESULT|"
COMMAND_TIMEOUT_SECONDS = 10.0
CURRENT_TIME = 0
MAX_REPORT_BYTES = 1024 * 1024

ASCII_KEYSYM_NAMES = {
    " ": "space",
    "!": "exclam",
    '"': "quotedbl",
    "#": "numbersign",
    "$": "dollar",
    "%": "percent",
    "&": "ampersand",
    "'": "apostrophe",
    "(": "parenleft",
    ")": "parenright",
    "*": "asterisk",
    "+": "plus",
    ",": "comma",
    "-": "minus",
    ".": "period",
    "/": "slash",
    ":": "colon",
    ";": "semicolon",
    "<": "less",
    "=": "equal",
    ">": "greater",
    "?": "question",
    "@": "at",
    "[": "bracketleft",
    "\\": "backslash",
    "]": "bracketright",
    "^": "asciicircum",
    "_": "underscore",
    "`": "grave",
    "{": "braceleft",
    "|": "bar",
    "}": "braceright",
    "~": "asciitilde",
}
SHIFTED_ASCII = frozenset('!@#$%^&*()_+{}|:"<>?~')


class HarnessError(RuntimeError):
    """An unmet measurement precondition or a failed real-app observation."""


@dataclass(frozen=True)
class Scenario:
    name: str
    method: str
    encoded_input: str
    expected: str


class ProbeReportState:
    """Receive application-observed values without browser title throttling."""

    def __init__(self) -> None:
        self.condition = threading.Condition()
        self.values: dict[str, str] = {}

    def record(self, token: str, value: str) -> None:
        with self.condition:
            self.values[token] = value
            self.condition.notify_all()

    def wait_for(
        self, token: str, expected: str, timeout_seconds: float
    ) -> str | None:
        deadline = time.monotonic() + timeout_seconds
        with self.condition:
            while self.values.get(token) != expected:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return self.values.get(token)
                self.condition.wait(remaining)
            return expected


class ProbeReportServer:
    """Own the loopback endpoint used by the browser comparison probe."""

    def __init__(self, port: int) -> None:
        self.state = ProbeReportState()
        state = self.state

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def do_POST(self) -> None:
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                except ValueError:
                    length = 0
                if self.path != "/report" or not 0 < length <= MAX_REPORT_BYTES:
                    self.send_error(400)
                    return
                try:
                    report = json.loads(self.rfile.read(length).decode("utf-8"))
                    token = report["token"]
                    value = report["value"]
                    if not isinstance(token, str) or not isinstance(value, str):
                        raise TypeError
                except (json.JSONDecodeError, KeyError, TypeError, UnicodeDecodeError):
                    self.send_error(400)
                    return
                state.record(token, value)
                self.send_response(204)
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", "0")
                self.end_headers()

            def log_message(self, _format: str, *_arguments: object) -> None:
                pass

        try:
            self.server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        except OSError as error:
            raise HarnessError(
                f"could not bind comparison probe report port {port}: {error}"
            ) from error
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self) -> ProbeReportServer:
        self.thread.start()
        return self

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


class XTestInjector:
    """Inject timed key events over one persistent XTEST connection."""

    def __init__(self) -> None:
        x11_path = ctypes.util.find_library("X11")
        xtst_path = ctypes.util.find_library("Xtst")
        if x11_path is None or xtst_path is None:
            raise HarnessError(
                "X11 and XTEST libraries are required by the desktop test harness"
            )
        self.x11 = ctypes.CDLL(x11_path)
        self.xtst = ctypes.CDLL(xtst_path)
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
        self.x11.XStringToKeysym.restype = ctypes.c_ulong
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_ubyte
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.xtst.XTestFakeKeyEvent.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint,
            ctypes.c_int,
            ctypes.c_ulong,
        ]
        self.xtst.XTestFakeKeyEvent.restype = ctypes.c_int
        self.display = self.x11.XOpenDisplay(None)
        if not self.display:
            raise HarnessError("could not open DISPLAY for XTEST input injection")
        self.shift_keycode = self.keycode("Shift_L")

    def close(self) -> None:
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None

    def __enter__(self) -> XTestInjector:
        return self

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        self.close()

    def keycode(self, keysym_name: str) -> int:
        keysym = self.x11.XStringToKeysym(keysym_name.encode("ascii"))
        keycode = self.x11.XKeysymToKeycode(self.display, keysym)
        if not keysym or not keycode:
            raise HarnessError(f"X11 keyboard map has no key for {keysym_name!r}")
        return int(keycode)

    def key(self, keysym_name: str, *, shifted: bool = False) -> None:
        keycode = self.keycode(keysym_name)
        if shifted:
            self.fake_key(self.shift_keycode, True)
        self.fake_key(keycode, True)
        self.fake_key(keycode, False)
        if shifted:
            self.fake_key(self.shift_keycode, False)
        self.x11.XFlush(self.display)

    def fake_key(self, keycode: int, pressed: bool) -> None:
        if not self.xtst.XTestFakeKeyEvent(
            self.display, keycode, int(pressed), CURRENT_TIME
        ):
            raise HarnessError("XTEST rejected a synthetic key event")

    def ascii(self, character: str) -> None:
        if len(character) != 1 or not character.isascii():
            raise HarnessError(
                f"comparison corpus contains non-ASCII input: {character!r}"
            )
        keysym_name = ASCII_KEYSYM_NAMES.get(character, character)
        self.key(
            keysym_name,
            shifted=character.isupper() or character in SHIFTED_ASCII,
        )


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            check=check,
            text=True,
            capture_output=True,
            timeout=COMMAND_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise HarnessError(f"command timed out: {error.cmd}") from error
    except subprocess.CalledProcessError as error:
        raise HarnessError(
            f"command failed ({error.returncode}): {error.cmd}: {error.stderr.strip()}"
        ) from error


def command_text(command: list[str]) -> str:
    return run(command).stdout.strip()


def require_binary(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise HarnessError(
            f"missing required test tool: {name}; install it on the test host "
            "without adding it as a UniLume runtime dependency"
        )
    return path


def load_corpus(
    path: Path, method: str, selected_names: frozenset[str] = frozenset()
) -> list[Scenario]:
    scenarios: list[Scenario] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            raise HarnessError(f"{path}:{line_number}: expected four tab-separated fields")
        scenario = Scenario(*fields)
        if scenario.method == method and (
            not selected_names or scenario.name in selected_names
        ):
            scenarios.append(scenario)
    if not scenarios:
        raise HarnessError(f"{path}: no scenarios for method {method!r}")
    missing = selected_names.difference(scenario.name for scenario in scenarios)
    if missing:
        raise HarnessError(
            f"{path}: unknown {method} scenario(s): {', '.join(sorted(missing))}"
        )
    return scenarios


def active_input_method() -> str:
    return command_text(["fcitx5-remote", "-n"])


def switch_input_method(name: str, timeout_seconds: float) -> None:
    completed = run(["fcitx5-remote", "-s", name], check=False)
    if completed.returncode != 0:
        raise HarnessError(
            f"could not select input method {name!r}: {completed.stderr.strip()}"
        )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if active_input_method() == name:
            return
        time.sleep(0.02)
    raise HarnessError(
        f"Fcitx5 did not report {name!r} as the active input method before timeout"
    )


def window_title(window_id: str) -> str:
    return command_text(["xdotool", "getwindowname", window_id])


def probe_window_suffix(title: str, token: str) -> str:
    """Capture browser chrome appended to the initially empty probe title."""
    probe_prefix = f"{token}{PROBE_MARKER}"
    if not title.startswith(probe_prefix):
        raise HarnessError(
            f"probe title must start with token and marker {probe_prefix!r}: {title!r}"
        )
    return title[len(probe_prefix):]


def xdotool(*arguments: str) -> None:
    completed = run(["xdotool", *arguments], check=False)
    if completed.returncode != 0:
        raise HarnessError(f"xdotool failed: {completed.stderr.strip()}")


def emit_input(
    injector: XTestInjector, encoded: str, delay_milliseconds: int
) -> tuple[int, int]:
    """Send ASCII key events and explicit Backspace through the real X11 path."""
    events: list[tuple[str, str]] = []
    cursor = 0
    while cursor < len(encoded):
        marker = encoded.find("<BS>", cursor)
        if marker == -1:
            marker = len(encoded)
        text = encoded[cursor:marker]
        for character in text:
            events.append(("ascii", character))
        if marker == len(encoded):
            break
        events.append(("key", "BackSpace"))
        cursor = marker + len("<BS>")
    for index, (event_type, value) in enumerate(events):
        if event_type == "ascii":
            injector.ascii(value)
        else:
            injector.key(value)
        if delay_milliseconds and index + 1 < len(events):
            time.sleep(delay_milliseconds / 1000.0)
    return len(events), time.monotonic_ns()


def warm_input_path(
    window_id: str,
    token: str,
    delay_milliseconds: int,
    reset_settle_milliseconds: int,
    timeout_seconds: float,
    injector: XTestInjector,
    reports: ProbeReportState,
) -> None:
    """Prime the selected frontend/server before collecting timed samples."""
    xdotool("windowactivate", "--sync", window_id)
    xdotool("key", "--clearmodifiers", "Escape")
    time.sleep(reset_settle_milliseconds / 1000.0)
    xdotool("key", "--clearmodifiers", "ctrl+a")
    xdotool("key", "--clearmodifiers", "BackSpace")
    emit_input(injector, "a ", delay_milliseconds)
    xdotool("key", "--clearmodifiers", "Escape")
    time.sleep(reset_settle_milliseconds / 1000.0)
    xdotool("key", "--clearmodifiers", "ctrl+a")
    xdotool("key", "--clearmodifiers", "BackSpace")
    observed = reports.wait_for(token, "", timeout_seconds)
    if observed != "":
        raise HarnessError(
            f"input path did not settle after warm-up: {observed!r}"
        )


def process_snapshot(pid: int) -> tuple[int, int]:
    stat_fields = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8").split()
    ticks = int(stat_fields[13]) + int(stat_fields[14])
    status = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            return ticks, int(line.split()[1])
    raise HarnessError(f"could not read VmRSS for Fcitx5 PID {pid}")


def fcitx_pid() -> int:
    completed = run(["pgrep", "-n", "fcitx5"], check=False)
    if completed.returncode != 0:
        raise HarnessError("fcitx5 is not running")
    return int(completed.stdout.strip())


def read_os_release() -> str:
    path = Path("/etc/os-release")
    return path.read_text(encoding="utf-8") if path.exists() else "unknown"


def cpu_model() -> str:
    for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
        if line.startswith("model name"):
            return line.partition(":")[2].strip()
    return "unknown"


def git_commit(root: Path) -> str:
    completed = run(["git", "-C", str(root), "rev-parse", "HEAD"], check=False)
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def package_version(package: str) -> str:
    if shutil.which("dpkg-query") is None:
        return "unknown"
    completed = run(["dpkg-query", "-W", "-f=${Version}", package], check=False)
    return completed.stdout.strip() if completed.returncode == 0 else "not-installed"


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    position = fraction * (len(ordered) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize(samples: Iterable[dict[str, object]]) -> dict[str, object]:
    rows = list(samples)
    latencies = [int(row["completion_ns"]) for row in rows]
    keys = sum(int(row["key_events"]) for row in rows)
    total_seconds = sum(latencies) / 1_000_000_000.0
    cpu_seconds = sum(float(row["fcitx_cpu_seconds"]) for row in rows)
    rss_deltas = [int(row["fcitx_rss_after_kib"]) - int(row["fcitx_rss_before_kib"]) for row in rows]
    return {
        "scenarios": len(rows),
        "key_events": keys,
        "errors": sum(not bool(row["correct"]) for row in rows),
        "completion_ns": {
            "min": min(latencies),
            "p50": percentile(latencies, 0.50),
            "p95": percentile(latencies, 0.95),
            "p99": percentile(latencies, 0.99),
            "max": max(latencies),
            "mean": statistics.fmean(latencies),
        },
        "keys_per_second": keys / total_seconds if total_seconds else 0.0,
        "fcitx_cpu_seconds": cpu_seconds,
        "fcitx_rss_delta_kib": {
            "median": statistics.median(rss_deltas),
            "max": max(rss_deltas),
        },
    }


def paired_summary(samples: Iterable[dict[str, object]]) -> dict[str, object]:
    """Summarize matching candidate/reference observations, never run order."""
    pairs: dict[tuple[int, str], dict[str, dict[str, object]]] = {}
    for sample in samples:
        key = (int(sample["round"]), str(sample["scenario"]))
        side = str(sample["side"])
        pair = pairs.setdefault(key, {})
        if side in pair:
            raise HarnessError(f"duplicate {side} observation for round/scenario {key}")
        pair[side] = sample

    ratios: list[float] = []
    for key, pair in sorted(pairs.items()):
        if set(pair) != {"candidate", "reference"}:
            raise HarnessError(f"incomplete candidate/reference pair for round/scenario {key}")
        candidate_time = int(pair["candidate"]["completion_ns"])
        reference_time = int(pair["reference"]["completion_ns"])
        if reference_time <= 0:
            raise HarnessError(f"invalid reference completion time for round/scenario {key}")
        ratios.append(candidate_time / reference_time)

    return {
        "pairs": len(ratios),
        "candidate_over_reference_completion_ratio": {
            "p50": percentile(ratios, 0.50),
            "p95": percentile(ratios, 0.95),
            "p99": percentile(ratios, 0.99),
            "mean": statistics.fmean(ratios),
        },
        "candidate_faster_pair_fraction": sum(ratio < 1.0 for ratio in ratios) / len(ratios),
    }


def preflight(arguments: argparse.Namespace) -> dict[str, object]:
    if os.environ.get("XDG_SESSION_TYPE") != "x11" or not os.environ.get("DISPLAY"):
        raise HarnessError("this harness only supports a native X11 session")
    for binary in ("fcitx5", "fcitx5-remote", "xdotool", "pgrep"):
        require_binary(binary)
    if not Path("/proc").exists():
        raise HarnessError("/proc is required for Fcitx5 resource snapshots")
    scenarios = load_corpus(
        arguments.corpus, arguments.method, frozenset(arguments.scenario)
    )
    title = window_title(arguments.window_id)
    title_suffix = probe_window_suffix(title, arguments.token)
    window_pid = command_text(["xdotool", "getwindowpid", arguments.window_id])
    window_executable = Path(f"/proc/{window_pid}/exe")
    root = Path(__file__).resolve().parents[2]
    return {
        "schema_version": 1,
        "session": os.environ.get("XDG_SESSION_TYPE"),
        "display": os.environ.get("DISPLAY"),
        "fcitx_version": command_text(["fcitx5", "--version"]),
        "fcitx_lotus_package_version": package_version("fcitx5-lotus"),
        "active_input_method": active_input_method(),
        "corpus": str(arguments.corpus),
        "corpus_sha256": hashlib.sha256(arguments.corpus.read_bytes()).hexdigest(),
        "method": arguments.method,
        "scenario_count": len(scenarios),
        "selected_scenarios": sorted(arguments.scenario),
        "window_id": arguments.window_id,
        "window_pid": window_pid,
        "window_executable": str(window_executable.resolve()) if window_executable.exists() else "unknown",
        "probe_token": arguments.token,
        "window_title_suffix": title_suffix,
        "probe_report_port": arguments.report_port,
        "unilume_commit": git_commit(root),
        "os_release": read_os_release(),
        "cpu_model": cpu_model(),
    }


def compare(arguments: argparse.Namespace) -> dict[str, object]:
    environment = preflight(arguments)
    original_input_method = str(environment["active_input_method"])
    pid = fcitx_pid()
    scenarios = load_corpus(
        arguments.corpus, arguments.method, frozenset(arguments.scenario)
    )
    schedule: list[tuple[int, str, str]] = []
    randomizer = random.Random(arguments.seed)
    for round_index in range(arguments.rounds):
        pair = [("candidate", arguments.candidate), ("reference", arguments.reference)]
        randomizer.shuffle(pair)
        schedule.extend((round_index, label, input_method) for label, input_method in pair)

    samples: list[dict[str, object]] = []
    try:
        with (
            ProbeReportServer(arguments.report_port) as report_server,
            XTestInjector() as injector,
        ):
            for round_index, label, input_method in schedule:
                xdotool("windowactivate", "--sync", arguments.window_id)
                switch_input_method(input_method, arguments.timeout_seconds)
                time.sleep(arguments.switch_settle_milliseconds / 1000.0)
                warm_input_path(
                    arguments.window_id,
                    arguments.token,
                    arguments.delay_milliseconds,
                    arguments.reset_settle_milliseconds,
                    arguments.timeout_seconds,
                    injector,
                    report_server.state,
                )
                for scenario in scenarios:
                    xdotool("windowactivate", "--sync", arguments.window_id)
                    # Reset the input-method composition before clearing the
                    # document. Ctrl+A/Backspace alone only changes
                    # application text and can leak a reference engine's
                    # previous token into the next scenario.
                    xdotool(
                        "key",
                        "--clearmodifiers",
                        "Escape",
                    )
                    time.sleep(arguments.reset_settle_milliseconds / 1000.0)
                    xdotool("key", "--clearmodifiers", "ctrl+a")
                    xdotool("key", "--clearmodifiers", "BackSpace")
                    cleared = report_server.state.wait_for(
                        arguments.token, "", arguments.timeout_seconds
                    )
                    if cleared != "":
                        raise HarnessError(
                            f"probe did not clear before {scenario.name}: {cleared!r}"
                        )
                    ticks_before, rss_before = process_snapshot(pid)
                    started = time.monotonic_ns()
                    key_events, last_input_event_ns = emit_input(
                        injector,
                        scenario.encoded_input,
                        arguments.delay_milliseconds,
                    )
                    observed = report_server.state.wait_for(
                        arguments.token,
                        scenario.expected,
                        arguments.timeout_seconds,
                    )
                    completed = time.monotonic_ns()
                    wall_completion_ns = completed - started
                    scheduled_input_delay_ns = (
                        max(key_events - 1, 0)
                        * arguments.delay_milliseconds
                        * 1_000_000
                    )
                    ticks_after, rss_after = process_snapshot(pid)
                    ticks_per_second = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
                    samples.append({
                        "round": round_index,
                        "side": label,
                        "input_method": input_method,
                        "scenario": scenario.name,
                        "method": scenario.method,
                        "key_events": key_events,
                        "completion_ns":
                            max(completed - last_input_event_ns, 1),
                        "wall_completion_ns": wall_completion_ns,
                        "input_injection_ns": last_input_event_ns - started,
                        "scheduled_input_delay_ns": scheduled_input_delay_ns,
                        "fcitx_cpu_seconds":
                            (ticks_after - ticks_before) / ticks_per_second,
                        "fcitx_rss_before_kib": rss_before,
                        "fcitx_rss_after_kib": rss_after,
                        "expected": scenario.expected,
                        "observed": observed if observed is not None else "",
                        "correct": observed == scenario.expected,
                    })
    finally:
        if original_input_method:
            restore = run(["fcitx5-remote", "-s", original_input_method], check=False)
            if restore.returncode != 0:
                print(
                    f"warning: could not restore input method {original_input_method!r}: {restore.stderr.strip()}",
                    file=sys.stderr,
                )

    candidate = [sample for sample in samples if sample["side"] == "candidate"]
    reference = [sample for sample in samples if sample["side"] == "reference"]
    result = {
        "schema_version": 2,
        "harness": "compare_fcitx5_desktop.py",
        "environment": environment,
        "seed": arguments.seed,
        "rounds": arguments.rounds,
        "delay_milliseconds": arguments.delay_milliseconds,
        "switch_settle_milliseconds": arguments.switch_settle_milliseconds,
        "reset_settle_milliseconds": arguments.reset_settle_milliseconds,
        "candidate": {"input_method": arguments.candidate, "summary": summarize(candidate)},
        "reference": {"input_method": arguments.reference, "summary": summarize(reference)},
        "paired_summary": paired_summary(samples),
        "samples": samples,
    }
    result["slo_gate"] = evaluate_slo(
        result,
        cpu_noise_seconds=arguments.cpu_noise_seconds,
        rss_noise_kib=arguments.rss_noise_kib,
    )
    return result


def evaluate_slo(
    result: dict[str, object],
    *,
    cpu_noise_seconds: float,
    rss_noise_kib: int,
) -> dict[str, object]:
    candidate = result["candidate"]["summary"]  # type: ignore[index]
    reference = result["reference"]["summary"]  # type: ignore[index]
    candidate_latency = candidate["completion_ns"]  # type: ignore[index]
    reference_latency = reference["completion_ns"]  # type: ignore[index]
    checks = {
        "candidate_correct": candidate["errors"] == 0,  # type: ignore[index]
        "reference_correct": reference["errors"] == 0,  # type: ignore[index]
        "p50_at_least_5_percent_lower":
            candidate_latency["p50"] <= reference_latency["p50"] * 0.95,
        "p95_at_least_5_percent_lower":
            candidate_latency["p95"] <= reference_latency["p95"] * 0.95,
        "p99_at_least_5_percent_lower":
            candidate_latency["p99"] <= reference_latency["p99"] * 0.95,
        "throughput_at_least_5_percent_higher":
            candidate["keys_per_second"] >= reference["keys_per_second"] * 1.05,  # type: ignore[index]
        "cpu_within_noise":
            candidate["fcitx_cpu_seconds"]  # type: ignore[index]
            <= reference["fcitx_cpu_seconds"] + cpu_noise_seconds,  # type: ignore[index]
        "rss_within_noise":
            candidate["fcitx_rss_delta_kib"]["max"]  # type: ignore[index]
            <= reference["fcitx_rss_delta_kib"]["max"] + rss_noise_kib,  # type: ignore[index]
    }
    return {
        "thresholds": {
            "latency_improvement_fraction": 0.05,
            "throughput_improvement_fraction": 0.05,
            "cpu_noise_seconds": cpu_noise_seconds,
            "rss_noise_kib": rss_noise_kib,
        },
        "checks": checks,
        "overall_pass": all(checks.values()),
    }


def parser() -> argparse.ArgumentParser:
    root = Path(__file__).resolve().parents[2]
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--candidate", required=True, help="configured Fcitx5 name for UniLume")
    argument_parser.add_argument("--reference", required=True, help="configured Fcitx5 name for Lotus")
    argument_parser.add_argument("--window-id", required=True, help="X11 id of the focused comparison probe")
    argument_parser.add_argument("--token", required=True, help="unique hash token in the probe page URL")
    argument_parser.add_argument("--method", default="telex", choices=("telex", "vni", "viqr"))
    argument_parser.add_argument(
        "--scenario",
        action="append",
        default=[],
        help="run only this named corpus scenario; repeat for multiple scenarios",
    )
    argument_parser.add_argument("--corpus", type=Path, default=root / "benchmarks/comparison/fcitx5-e2e.tsv")
    argument_parser.add_argument("--rounds", type=int, default=5)
    argument_parser.add_argument("--seed", type=int, default=39)
    argument_parser.add_argument("--delay-milliseconds", type=int, default=1)
    argument_parser.add_argument(
        "--switch-settle-milliseconds",
        type=int,
        default=100,
        help="settling interval after selecting an input method",
    )
    argument_parser.add_argument(
        "--reset-settle-milliseconds",
        type=int,
        default=50,
        help="settling interval after resetting an input-method composition",
    )
    argument_parser.add_argument("--timeout-seconds", type=float, default=5.0)
    argument_parser.add_argument(
        "--report-port",
        type=int,
        default=38491,
        help="loopback port used by the browser probe to report observed text",
    )
    argument_parser.add_argument("--cpu-noise-seconds", type=float, default=0.05)
    argument_parser.add_argument("--rss-noise-kib", type=int, default=64)
    argument_parser.add_argument("--output", type=Path, help="write raw JSON result")
    argument_parser.add_argument("--preflight", action="store_true", help="validate the host without changing Fcitx5 state")
    argument_parser.add_argument(
        "--enforce-slo",
        action="store_true",
        help="return non-zero unless all correctness, latency, throughput and resource gates pass",
    )
    return argument_parser


def main() -> int:
    arguments = parser().parse_args()
    if arguments.rounds < 5:
        raise HarnessError("at least five paired rounds are required")
    if (
        arguments.delay_milliseconds < 0
        or arguments.switch_settle_milliseconds < 0
        or arguments.reset_settle_milliseconds < 0
        or arguments.timeout_seconds <= 0
        or not 1 <= arguments.report_port <= 65535
        or arguments.cpu_noise_seconds < 0
        or arguments.rss_noise_kib < 0
    ):
        raise HarnessError("delay must be non-negative and timeout must be positive")
    result = preflight(arguments) if arguments.preflight else compare(arguments)
    encoded = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded, encoding="utf-8")
    else:
        sys.stdout.write(encoded)
    if (
        arguments.enforce_slo
        and not arguments.preflight
        and not result["slo_gate"]["overall_pass"]
    ):
        return 3
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HarnessError as error:
        print(f"comparison harness error: {error}", file=sys.stderr)
        raise SystemExit(2)
