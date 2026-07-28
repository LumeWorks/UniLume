#!/usr/bin/env python3
"""Qualify the UniLume Fcitx5 addon on a real native Wayland compositor.

This harness is a qualification tool, not a production input path. It runs
inside an existing native Wayland session, injects real key events through the
compositor seat, and reads the exact bytes a real Wayland client received. A
compile check or a manual checklist is deliberately not accepted as evidence.

Key injection uses the ``zwp_virtual_keyboard_v1`` protocol through ``wtype``.
Compositors that do not implement that protocol cannot be driven from inside
the session, and the harness refuses to report a result for them rather than
substituting ``uinput``, which issue #58 places out of scope because it would
mask native-path behaviour.

Exact output extraction uses a terminal client whose pty is placed in raw mode
so the line discipline never erases a byte on the application's behalf. A
Backspace that UniLume failed to consume therefore surfaces as a literal
delete byte and fails the comparison instead of being silently absorbed.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CORPUS = REPOSITORY_ROOT / "benchmarks" / "comparison" / "fcitx5-e2e.tsv"
SHARED_HARNESS_PATH = (
    REPOSITORY_ROOT / "scripts" / "benchmark" / "compare_fcitx5_desktop.py"
)

# The corpus contract, the process snapshots and the environment metadata are
# display-server independent, so they are shared with the X11 comparison
# harness instead of being restated here. Only the injection and extraction
# layers differ between X11 and Wayland.
_SPEC = importlib.util.spec_from_file_location(
    "unilume_shared_harness", SHARED_HARNESS_PATH
)
if _SPEC is None or _SPEC.loader is None:
    raise SystemExit(f"cannot load shared harness helpers from {SHARED_HARNESS_PATH}")
_SHARED = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _SHARED
_SPEC.loader.exec_module(_SHARED)

HarnessError = _SHARED.HarnessError
Scenario = _SHARED.Scenario
load_corpus = _SHARED.load_corpus
run = _SHARED.run
command_text = _SHARED.command_text
require_binary = _SHARED.require_binary
percentile = _SHARED.percentile
read_os_release = _SHARED.read_os_release
cpu_model = _SHARED.cpu_model
git_commit = _SHARED.git_commit
package_version = _SHARED.package_version
fcitx_pid = _SHARED.fcitx_pid
active_input_method = _SHARED.active_input_method

BACKSPACE_MARKER = "<BS>"
CLIENT_READY_TIMEOUT_SECONDS = 20.0
CLIENT_POLL_SECONDS = 0.02
SOAK_SAMPLE_SECONDS = 5.0
DIAGNOSTIC_WAIT_SECONDS = 5.0

# wtype treats a leading dash as an option, so a corpus chunk that begins with
# one could silently become a flag instead of typed text.
UNSAFE_TEXT_PREFIX = "-"

WLROOTS_COMPOSITORS = frozenset({"sway", "river", "labwc", "cage", "hyprland", "wayfire"})

# A readiness probe that only passes when the engine really transformed the
# keystrokes. Selecting the input method is not enough evidence that UniLume is
# in the path, because raw passthrough reproduces plain ASCII unchanged.
READINESS_PROBE = {
    "telex": ("aa", "â"),
    "vni": ("a6", "â"),
    "viqr": ("a^", "â"),
}


class QualificationError(HarnessError):
    """An unmet qualification precondition or a failed real-client observation."""


@dataclass(frozen=True)
class ObservedScenario:
    name: str
    method: str
    expected: str
    observed: str
    before_boundary: str
    key_events: int
    completion_ns: int

    @property
    def correct(self) -> bool:
        return self.observed == self.expected

    @property
    def zero_preedit(self) -> bool:
        """True when the client held the final text before any commit boundary."""
        return self.before_boundary == self.expected

    @property
    def defect(self) -> str:
        return classify_defect(self.expected, self.observed)


def classify_defect(expected: str, observed: str) -> str:
    """Name the corruption class of an incorrect observation.

    The classes are the ones issue #58 gates on. ``lost`` means every observed
    character still appears in order but something is missing, ``duplicate``
    means an expected character was emitted more times than expected, and
    ``reordered`` means the multiset matches but the sequence does not. Anything
    carrying a character the scenario never asked for is ``corrupted``, which
    covers an unconsumed delete byte or a substituted character.
    """
    if observed == expected:
        return "none"
    if not observed:
        return "lost"
    expected_counts: dict[str, int] = {}
    observed_counts: dict[str, int] = {}
    for character in expected:
        expected_counts[character] = expected_counts.get(character, 0) + 1
    for character in observed:
        observed_counts[character] = observed_counts.get(character, 0) + 1
    if any(character not in expected_counts for character in observed_counts):
        return "corrupted"
    if expected_counts == observed_counts:
        return "reordered"
    if any(
        count > expected_counts[character]
        for character, count in observed_counts.items()
    ):
        return "duplicate"
    if is_subsequence(observed, expected):
        return "lost"
    return "corrupted"


def is_subsequence(candidate: str, reference: str) -> bool:
    iterator = iter(reference)
    return all(character in iterator for character in candidate)


def split_input(encoded: str) -> list[tuple[str, str]]:
    """Split a corpus entry into ordered text and explicit Backspace events."""
    events: list[tuple[str, str]] = []
    cursor = 0
    while cursor < len(encoded):
        marker = encoded.find(BACKSPACE_MARKER, cursor)
        if marker == -1:
            text = encoded[cursor:]
            if text:
                events.append(("text", text))
            break
        text = encoded[cursor:marker]
        if text:
            events.append(("text", text))
        events.append(("key", "BackSpace"))
        cursor = marker + len(BACKSPACE_MARKER)
    return events


def count_key_events(encoded: str) -> int:
    return sum(
        len(value) if kind == "text" else 1 for kind, value in split_input(encoded)
    )


def wtype_arguments(encoded: str, delay_milliseconds: int) -> list[str]:
    """Build one wtype invocation that reproduces a corpus entry in order.

    A non-positive delay omits the flag rather than passing zero, which wtype
    rejects. Omitting it is also the real burst case, because it sends the
    keystrokes with no sleep at all instead of an artificial one-millisecond
    floor.
    """
    arguments = ["wtype"]
    if delay_milliseconds > 0:
        arguments.extend(["-d", str(delay_milliseconds)])
    for kind, value in split_input(encoded):
        if kind == "text":
            if value.startswith(UNSAFE_TEXT_PREFIX):
                raise QualificationError(
                    f"corpus text chunk starts with {UNSAFE_TEXT_PREFIX!r} and "
                    f"cannot be injected unambiguously: {value!r}"
                )
            if not value.isascii():
                raise QualificationError(
                    f"corpus input must be ASCII keystrokes, not composed text: {value!r}"
                )
            arguments.append(value)
        else:
            arguments.extend(["-k", value])
    return arguments


class TerminalClient:
    """Own a native Wayland terminal whose received bytes are captured exactly.

    The pty is switched to raw mode before the reader starts so the kernel line
    discipline neither echoes nor erases, which keeps a Backspace that the
    input method failed to consume visible as a delete byte.
    """

    def __init__(self, terminal: str, capture: Path, log: Path) -> None:
        self.terminal = terminal
        self.capture = capture
        self.log = log
        self.process: subprocess.Popen[bytes] | None = None
        self.offset = 0

    def __enter__(self) -> TerminalClient:
        self.capture.write_bytes(b"")
        marker = self.capture.with_suffix(".ready")
        marker.unlink(missing_ok=True)
        script = (
            f"stty raw -echo; : > {marker!s}; exec cat > {self.capture!s}"
        )
        with self.log.open("wb") as log_stream:
            self.process = subprocess.Popen(
                [self.terminal, "--", "sh", "-c", script],
                stdout=log_stream,
                stderr=subprocess.STDOUT,
            )
        deadline = time.monotonic() + CLIENT_READY_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if marker.exists():
                return self
            if self.process.poll() is not None:
                raise QualificationError(
                    f"{self.terminal} exited before the capture shell was ready: "
                    f"{self.log.read_text(encoding='utf-8', errors='replace').strip()}"
                )
            time.sleep(CLIENT_POLL_SECONDS)
        raise QualificationError(
            f"{self.terminal} did not become ready within "
            f"{CLIENT_READY_TIMEOUT_SECONDS:.0f}s"
        )

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def alive(self) -> bool:
        return self.process is not None and self.process.poll() is None

    def reset(self) -> None:
        """Start a new scenario without truncating the capture file.

        Truncating is not an option: the reader keeps its own file offset, so a
        truncated file is refilled with a hole of NUL bytes at the next write.
        Advance a read offset instead and only compare what arrives after it.
        """
        self.offset = self.capture.stat().st_size

    def raw(self) -> str:
        data = self.capture.read_bytes()[self.offset:]
        return data.decode("utf-8", errors="replace")

    def text(self) -> str:
        """Return the received text without the trailing commit boundary.

        The harness ends every scenario with Return to force any pending
        preedit to commit. The line discipline turns that into a newline, which
        is transport, not engine output, so it is not compared.
        """
        return self.raw().rstrip("\r\n")

    def wait_for(self, expected: str, timeout_seconds: float) -> tuple[str, int]:
        """Settle on the client's received text and report the settle time."""
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            current = self.text()
            if current == expected:
                return current, time.monotonic_ns()
            time.sleep(CLIENT_POLL_SECONDS)
        return self.text(), time.monotonic_ns()


def inject(encoded: str, delay_milliseconds: int) -> int:
    completed = subprocess.run(
        wtype_arguments(encoded, delay_milliseconds),
        check=False,
        text=True,
        capture_output=True,
        timeout=_SHARED.COMMAND_TIMEOUT_SECONDS,
    )
    if completed.returncode != 0:
        raise QualificationError(
            f"wtype could not inject through the compositor seat: "
            f"{completed.stderr.strip()}"
        )
    return time.monotonic_ns()


def inject_commit_boundary() -> None:
    """Force any pending preedit to commit with a real Return key."""
    completed = subprocess.run(
        ["wtype", "-k", "Return"],
        check=False,
        text=True,
        capture_output=True,
        timeout=_SHARED.COMMAND_TIMEOUT_SECONDS,
    )
    if completed.returncode != 0:
        raise QualificationError(
            f"wtype could not inject the commit boundary: {completed.stderr.strip()}"
        )


def select_input_method(name: str, timeout_seconds: float) -> None:
    """Select and activate an input method for a freshly mapped Wayland client.

    Fcitx tracks the active input method per input context. A Wayland client
    receives focus asynchronously after it maps, so a single selection issued
    before the context exists is silently dropped. Retry the selection until
    Fcitx confirms it rather than polling a request that never landed.
    """
    deadline = time.monotonic() + timeout_seconds
    last_seen = ""
    while time.monotonic() < deadline:
        run(["fcitx5-remote", "-s", name], check=False)
        run(["fcitx5-remote", "-o"], check=False)
        last_seen = active_input_method()
        if last_seen == name:
            return
        time.sleep(CLIENT_POLL_SECONDS)
    raise QualificationError(
        f"Fcitx did not confirm {name!r} as the active input method for the "
        f"Wayland client within {timeout_seconds:.0f}s; last reported "
        f"{last_seen!r}"
    )


def verify_engine_in_path(
    client: TerminalClient, method: str, timeout_seconds: float
) -> None:
    """Prove the engine transforms keystrokes before measuring anything.

    This also primes the frontend, so the first measured scenario does not
    absorb one-time text-input negotiation with the compositor.
    """
    if method not in READINESS_PROBE:
        raise QualificationError(f"no readiness probe defined for method {method!r}")
    encoded, expected = READINESS_PROBE[method]
    client.reset()
    inject(encoded, 0)
    # Send the boundary too, so a preedit fallback still proves the engine ran.
    inject_commit_boundary()
    observed, _ = client.wait_for(expected, timeout_seconds)
    if observed != expected:
        raise QualificationError(
            f"the input method is selected but did not transform {encoded!r} into "
            f"{expected!r}; the client received {observed!r}. UniLume is not in "
            "the Wayland input path, so no result would be meaningful"
        )


def compositor_identity() -> dict[str, str]:
    """Name the running compositor without trusting a single hint."""
    desktop = os.environ.get("XDG_CURRENT_DESKTOP", "")
    session = os.environ.get("XDG_SESSION_DESKTOP", "")
    detected = "unknown"
    version = "unknown"
    for candidate in sorted(WLROOTS_COMPOSITORS) + ["kwin_wayland", "gnome-shell"]:
        if candidate in (desktop.lower(), session.lower()) or (
            os.environ.get(f"{candidate.upper()}SOCK")
        ):
            detected = candidate
            break
    if detected == "unknown":
        for candidate in ("sway", "kwin_wayland", "gnome-shell", "river", "labwc"):
            completed = run(["pgrep", "-x", candidate], check=False)
            if completed.returncode == 0:
                detected = candidate
                break
    if detected != "unknown" and shutil.which(detected) is not None:
        completed = run([detected, "--version"], check=False)
        text = (completed.stdout or completed.stderr).strip()
        match = re.search(r"\d+\.\d+(?:\.\d+)?", text)
        version = match.group(0) if match else (text.splitlines() or ["unknown"])[0]
    family = "wlroots" if detected in WLROOTS_COMPOSITORS else {
        "kwin_wayland": "kwin",
        "gnome-shell": "mutter",
    }.get(detected, "unknown")
    return {"compositor": detected, "compositor_version": version, "family": family}


def preflight(arguments: argparse.Namespace) -> dict[str, object]:
    """Refuse to produce evidence unless the session is really native Wayland."""
    if not os.environ.get("WAYLAND_DISPLAY"):
        raise QualificationError(
            "WAYLAND_DISPLAY is unset; this harness qualifies native Wayland only"
        )
    session_type = os.environ.get("XDG_SESSION_TYPE", "")
    if session_type not in ("wayland", ""):
        raise QualificationError(
            f"XDG_SESSION_TYPE={session_type!r} is not a native Wayland session"
        )
    for binary in ("wtype", arguments.terminal, "fcitx5", "fcitx5-remote", "pgrep"):
        require_binary(binary)
    if not Path("/proc").exists():
        raise QualificationError("/proc is required for Fcitx5 resource snapshots")
    scenarios = load_corpus(
        arguments.corpus, arguments.method, frozenset(arguments.scenario)
    )
    identity = compositor_identity()
    environment: dict[str, object] = {
        "schema_version": 1,
        "session": session_type or "wayland",
        "wayland_display": os.environ.get("WAYLAND_DISPLAY"),
        "xwayland_display": os.environ.get("DISPLAY") or "none",
        "injection_protocol": "zwp_virtual_keyboard_v1",
        "extraction": f"{arguments.terminal} raw-mode pty capture",
        "fcitx_version": command_text(["fcitx5", "--version"]),
        "fcitx_package_version": package_version("fcitx5"),
        "terminal": arguments.terminal,
        "terminal_package_version": package_version(arguments.terminal),
        "active_input_method": active_input_method(),
        "corpus": str(arguments.corpus),
        "corpus_sha256": hashlib.sha256(arguments.corpus.read_bytes()).hexdigest(),
        "method": arguments.method,
        "scenario_count": len(scenarios),
        "selected_scenarios": sorted(arguments.scenario),
        "unilume_commit": git_commit(REPOSITORY_ROOT),
        "os_release": read_os_release(),
        "cpu_model": cpu_model(),
    }
    environment.update(identity)
    return environment


def process_snapshot(pid: int) -> dict[str, int]:
    root = Path(f"/proc/{pid}")
    status = (root / "status").read_text(encoding="utf-8")
    values = {"rss_kib": 0, "threads": 0}
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            values["rss_kib"] = int(line.split()[1])
        elif line.startswith("Threads:"):
            values["threads"] = int(line.split()[1])
    if not values["rss_kib"] or not values["threads"]:
        raise QualificationError(f"incomplete Fcitx5 resource snapshot for PID {pid}")
    return values


def observe_scenario(
    client: TerminalClient,
    scenario: Scenario,
    delay_milliseconds: int,
    timeout_seconds: float,
    preedit_settle_seconds: float,
) -> ObservedScenario:
    """Observe one scenario and record whether the client ever saw a preedit.

    The text is sampled twice. The first sample happens before any commit
    boundary is sent, so text that is already present proves UniLume replaced
    the composition directly. If the text only appears after the boundary, the
    run observed the preedit fallback instead of the zero-preedit path, and the
    result says so rather than reporting an indistinguishable pass.
    """
    client.reset()
    last_input_ns = inject(scenario.encoded_input, delay_milliseconds)
    before_boundary, _ = client.wait_for(scenario.expected, preedit_settle_seconds)
    inject_commit_boundary()
    observed, settled_ns = client.wait_for(scenario.expected, timeout_seconds)
    return ObservedScenario(
        name=scenario.name,
        method=scenario.method,
        expected=scenario.expected,
        observed=observed,
        before_boundary=before_boundary,
        key_events=count_key_events(scenario.encoded_input),
        completion_ns=max(settled_ns - last_input_ns, 1),
    )


def summarize(observations: Sequence[ObservedScenario]) -> dict[str, object]:
    if not observations:
        raise QualificationError("no observations were collected")
    latencies = [observation.completion_ns for observation in observations]
    defects: dict[str, int] = {}
    for observation in observations:
        if not observation.correct:
            defects[observation.defect] = defects.get(observation.defect, 0) + 1
    correct = [
        observation for observation in observations if observation.correct
    ]
    return {
        "observations": len(observations),
        "key_events": sum(observation.key_events for observation in observations),
        "errors": sum(not observation.correct for observation in observations),
        "defects": defects,
        "zero_preedit_observations": sum(
            observation.zero_preedit for observation in correct
        ),
        "preedit_fallback_observations": sum(
            not observation.zero_preedit for observation in correct
        ),
        "completion_ns": {
            "min": min(latencies),
            "p50": percentile(latencies, 0.50),
            "p95": percentile(latencies, 0.95),
            "max": max(latencies),
        },
    }


def as_samples(observations: Sequence[ObservedScenario]) -> list[dict[str, object]]:
    return [
        {
            "scenario": observation.name,
            "method": observation.method,
            "expected": observation.expected,
            "observed": observation.observed,
            "text_before_commit_boundary": observation.before_boundary,
            "correct": observation.correct,
            "zero_preedit": observation.zero_preedit,
            "defect": observation.defect,
            "key_events": observation.key_events,
            "completion_ns": observation.completion_ns,
        }
        for observation in observations
    ]


def run_corpus(
    client: TerminalClient,
    scenarios: Sequence[Scenario],
    arguments: argparse.Namespace,
) -> list[ObservedScenario]:
    return [
        observe_scenario(
            client,
            scenario,
            arguments.delay_milliseconds,
            arguments.timeout_seconds,
            arguments.preedit_settle_seconds,
        )
        for scenario in scenarios
    ]


def run_burst(
    client: TerminalClient,
    scenarios: Sequence[Scenario],
    arguments: argparse.Namespace,
    delay_milliseconds: int,
) -> list[ObservedScenario]:
    """Repeat the corpus at a fixed key rate to expose ordering defects."""
    observations: list[ObservedScenario] = []
    for _ in range(arguments.burst_rounds):
        for scenario in scenarios:
            observations.append(
                observe_scenario(
                    client,
                    scenario,
                    delay_milliseconds,
                    arguments.timeout_seconds,
                    arguments.preedit_settle_seconds,
                )
            )
    return observations


def run_stress(
    client: TerminalClient,
    scenarios: Sequence[Scenario],
    arguments: argparse.Namespace,
) -> list[ObservedScenario]:
    """Send the corpus as fast as the protocol allows, beyond the burst gate.

    Issue #58 specifies its burst gate at one millisecond per key. This phase
    deliberately exceeds any human rate, so its defects are reported as
    findings rather than folded into the pass or fail verdict.
    """
    observations: list[ObservedScenario] = []
    for _ in range(arguments.stress_rounds):
        for scenario in scenarios:
            observations.append(
                observe_scenario(
                    client,
                    scenario,
                    0,
                    arguments.timeout_seconds,
                    arguments.preedit_settle_seconds,
                )
            )
    return observations


def run_soak(
    client: TerminalClient,
    scenarios: Sequence[Scenario],
    arguments: argparse.Namespace,
    pid: int,
) -> dict[str, object]:
    """Type continuously for a fixed duration and bound Fcitx resource use."""
    deadline = time.monotonic() + arguments.soak_seconds
    samples: list[dict[str, int]] = [process_snapshot(pid)]
    next_sample = time.monotonic() + SOAK_SAMPLE_SECONDS
    observations: list[ObservedScenario] = []
    while time.monotonic() < deadline:
        for scenario in scenarios:
            if time.monotonic() >= deadline:
                break
            observations.append(
                observe_scenario(
                    client,
                    scenario,
                    arguments.delay_milliseconds,
                    arguments.timeout_seconds,
                    arguments.preedit_settle_seconds,
                )
            )
            if not client.alive():
                raise QualificationError("the Wayland client exited during the soak")
            if time.monotonic() >= next_sample:
                samples.append(process_snapshot(pid))
                next_sample = time.monotonic() + SOAK_SAMPLE_SECONDS
    samples.append(process_snapshot(pid))
    rss = [sample["rss_kib"] for sample in samples]
    return {
        "requested_seconds": arguments.soak_seconds,
        "qualifying": arguments.soak_seconds >= arguments.qualifying_soak_seconds,
        "summary": summarize(observations),
        "resource_samples": len(samples),
        "rss_kib": {"first": rss[0], "last": rss[-1], "max": max(rss)},
        "rss_growth_kib": rss[-1] - rss[0],
        "threads": {
            "first": samples[0]["threads"],
            "last": samples[-1]["threads"],
        },
    }


def read_diagnostic_bundle(
    path: Path, wait_seconds: float = DIAGNOSTIC_WAIT_SECONDS
) -> dict[str, object]:
    """Read the addon's own bounded trace so the backend path is observable.

    The addon exports the bundle when an input context is destroyed rather than
    in the key path, so the export races with the client shutdown that triggers
    it. Wait a bounded time for the file instead of reporting it as missing.
    """
    deadline = time.monotonic() + wait_seconds
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(CLIENT_POLL_SECONDS)
    if not path.exists():
        return {
            "available": False,
            "reason": (
                "no diagnostic bundle was exported; run Fcitx with "
                "UNILUME_FCITX_DIAGNOSTICS=1 and UNILUME_FCITX_DIAGNOSTIC_FILE "
                "and let the input context be destroyed"
            ),
        }
    try:
        bundle = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise QualificationError(f"diagnostic bundle {path} is not valid JSON: {error}")
    events = bundle.get("events", [])
    paths: dict[str, int] = {}
    capabilities: dict[str, int] = {}
    for event in events:
        if not isinstance(event, dict):
            continue
        name = str(event.get("path", "unknown"))
        paths[name] = paths.get(name, 0) + 1
        capability = str(event.get("capability", "unknown"))
        if capability not in ("none", "unknown"):
            capabilities[capability] = capabilities.get(capability, 0) + 1
    return {
        "available": True,
        "session": bundle.get("session"),
        "unilume_version": bundle.get("unilume_version"),
        "fcitx_version": bundle.get("fcitx_version"),
        "total_events": bundle.get("total_events"),
        "fallbacks": bundle.get("fallbacks"),
        "stale_results": bundle.get("stale_results"),
        "uncertain_outcomes": bundle.get("uncertain_outcomes"),
        "backend_failures": bundle.get("backend_failures"),
        "capability_losses": bundle.get("capability_losses"),
        "observed_paths": paths,
        "observed_capability_gates": capabilities,
    }


def observed_backend_path(summary: dict[str, object]) -> str:
    """Name the backend path the client actually experienced."""
    direct = int(summary["zero_preedit_observations"])  # type: ignore[arg-type]
    fallback = int(summary["preedit_fallback_observations"])  # type: ignore[arg-type]
    if direct and not fallback:
        return "direct"
    if fallback and not direct:
        return "preedit"
    if direct and fallback:
        return "mixed"
    return "none"


def diagnostic_backend_path(diagnostics: dict[str, object]) -> str:
    """Name the backend path the addon's own trace recorded."""
    paths = diagnostics.get("observed_paths")
    if not isinstance(paths, dict) or not paths:
        return "unknown"
    engaged = {name: count for name, count in paths.items() if name != "off"}
    if not engaged:
        return "off"
    if len(engaged) == 1:
        return next(iter(engaged))
    return "mixed"


def evaluate(result: dict[str, object]) -> dict[str, object]:
    """Decide which issue #58 acceptance criteria this run actually satisfied."""
    corpus = result["corpus"]["summary"]  # type: ignore[index]
    burst = result["burst"]["summary"]  # type: ignore[index]
    soak = result["soak"]
    diagnostics = result["diagnostics"]
    exact_defects = {"lost", "duplicate", "reordered", "corrupted"}
    client_path = observed_backend_path(corpus)
    trace_path = diagnostic_backend_path(diagnostics)  # type: ignore[arg-type]
    available = bool(diagnostics.get("available"))  # type: ignore[union-attr]
    checks = {
        "corpus_exact_output": corpus["errors"] == 0,  # type: ignore[index]
        "burst_no_lost_duplicate_reordered": not exact_defects.intersection(
            burst["defects"]  # type: ignore[index]
        ),
        "client_survived": bool(result["client_survived"]),
        "fallback_reason_observable": available
        and diagnostics.get("session") == "wayland",  # type: ignore[union-attr]
        "no_backend_failures": diagnostics.get("backend_failures", 0) == 0  # type: ignore[union-attr]
        if available
        else False,
        # The addon must not report a path the client did not experience.
        "backend_path_agrees_with_client": available
        and trace_path in (client_path, "mixed"),
    }
    if soak is not None:
        checks["soak_correct"] = soak["summary"]["errors"] == 0  # type: ignore[index]
        checks["soak_qualifying_duration"] = bool(soak["qualifying"])  # type: ignore[index]
    unmet = sorted(name for name, passed in checks.items() if not passed)
    return {
        "checks": checks,
        "unmet": unmet,
        "overall_pass": not unmet,
        "client_observed_backend_path": client_path,
        "diagnostic_backend_path": trace_path,
        "compositor_families_claimed": [result["environment"]["family"]],  # type: ignore[index]
        "note": (
            "This run qualifies only the compositor recorded in environment. "
            "It is not evidence for any other compositor or version."
        ),
    }


def qualify(arguments: argparse.Namespace) -> dict[str, object]:
    environment = preflight(arguments)
    scenarios = load_corpus(
        arguments.corpus, arguments.method, frozenset(arguments.scenario)
    )
    original_input_method = str(environment["active_input_method"])
    pid = fcitx_pid()
    capture = arguments.work_directory / "client-capture.txt"
    log = arguments.work_directory / "client.log"
    arguments.work_directory.mkdir(parents=True, exist_ok=True)

    try:
        with TerminalClient(arguments.terminal, capture, log) as client:
            # Select the input method only once a real client owns the focus,
            # then prove the engine is actually transforming keystrokes.
            select_input_method(arguments.input_method, arguments.timeout_seconds)
            verify_engine_in_path(
                client, arguments.method, arguments.timeout_seconds
            )
            corpus_observations = run_corpus(client, scenarios, arguments)
            burst_observations = run_burst(
                client, scenarios, arguments, arguments.burst_delay_milliseconds
            )
            stress_observations = (
                run_stress(client, scenarios, arguments)
                if arguments.stress_rounds > 0
                else []
            )
            soak = (
                run_soak(client, scenarios, arguments, pid)
                if arguments.soak_seconds > 0
                else None
            )
            client_survived = client.alive()
    finally:
        if original_input_method:
            restore = run(
                ["fcitx5-remote", "-s", original_input_method], check=False
            )
            if restore.returncode != 0:
                print(
                    "warning: could not restore input method "
                    f"{original_input_method!r}: {restore.stderr.strip()}",
                    file=sys.stderr,
                )

    result: dict[str, object] = {
        "schema_version": 1,
        "harness": "qualify_wayland_compositor.py",
        "environment": environment,
        "input_method": arguments.input_method,
        "delay_milliseconds": arguments.delay_milliseconds,
        "burst_rounds": arguments.burst_rounds,
        "burst_delay_milliseconds": arguments.burst_delay_milliseconds,
        "stress_rounds": arguments.stress_rounds,
        "client_survived": client_survived,
        "corpus": {
            "summary": summarize(corpus_observations),
            "samples": as_samples(corpus_observations),
        },
        "burst": {
            "summary": summarize(burst_observations),
            "failures": [
                sample
                for sample in as_samples(burst_observations)
                if not sample["correct"]
            ],
        },
        "stress": (
            {
                "note": (
                    "Sent with no scheduled delay, faster than the one "
                    "millisecond per key that issue #58 gates on. Defects here "
                    "are reported as findings and do not set overall_pass."
                ),
                "summary": summarize(stress_observations),
                "failures": [
                    sample
                    for sample in as_samples(stress_observations)
                    if not sample["correct"]
                ],
            }
            if stress_observations
            else None
        ),
        "soak": soak,
        "diagnostics": read_diagnostic_bundle(arguments.diagnostic_file),
    }
    result["qualification"] = evaluate(result)
    return result


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument("--method", default="telex")
    parser.add_argument("--scenario", action="append", default=[])
    parser.add_argument("--input-method", default="unilume")
    parser.add_argument("--terminal", default="foot")
    parser.add_argument("--delay-milliseconds", type=int, default=10)
    parser.add_argument("--burst-rounds", type=int, default=10)
    parser.add_argument(
        "--burst-delay-milliseconds",
        type=int,
        default=1,
        help="gated burst key rate; issue #58 specifies one millisecond per key",
    )
    parser.add_argument(
        "--stress-rounds",
        type=int,
        default=3,
        help="extra rounds sent with no delay at all, reported but not gated",
    )
    parser.add_argument("--soak-seconds", type=float, default=0.0)
    parser.add_argument(
        "--qualifying-soak-seconds",
        type=float,
        default=1800.0,
        help="minimum soak length issue #58 accepts as qualifying evidence",
    )
    parser.add_argument("--timeout-seconds", type=float, default=10.0)
    parser.add_argument(
        "--preedit-settle-seconds",
        type=float,
        default=0.5,
        help=(
            "time to wait for directly replaced text before sending the commit "
            "boundary that would also flush a preedit"
        ),
    )
    parser.add_argument("--work-directory", type=Path, default=Path("/tmp/unilume-wayland"))
    parser.add_argument(
        "--diagnostic-file",
        type=Path,
        default=Path("/tmp/unilume-wayland/unilume-diagnostic.json"),
    )
    parser.add_argument("--output", type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    try:
        result = qualify(arguments)
    except HarnessError as error:
        print(f"qualification failed: {error}", file=sys.stderr)
        return 2
    report = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True)
    if arguments.output:
        arguments.output.write_text(report + "\n", encoding="utf-8")
    print(report)
    qualification = result["qualification"]
    return 0 if qualification["overall_pass"] else 1  # type: ignore[index]


if __name__ == "__main__":
    sys.exit(main())
