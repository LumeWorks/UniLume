<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Fair UniLume and Fcitx5 Lotus comparison protocol

This document is the preregistered protocol for claims that UniLume is faster
or more stable than Fcitx5 Lotus. It complements the core and deterministic
integration benchmarks; neither of those benchmarks may make a Lotus claim.

Issue #39 established this protocol. A result is release evidence only when
the raw observations and environment metadata below are retained with the
qualifying run.

## Non-negotiable correctness gate

Every paired run must use the same physical machine, kernel, Fcitx5 version,
desktop session, browser/app version, keyboard layout, corpus, input-event
delay, build type, and warm-up procedure. The candidate and reference run in a
randomized order for at least five paired rounds.

For every scenario UniLume must have all of the following:

- exact observed text equal to the corpus output;
- valid UTF-8;
- no timeout, crash, or freeze;
- zero lost, duplicated, and reordered text;
- no unresolved Fcitx transaction or queue error in the accompanying UniLume
  diagnostic output.

Any correctness failure invalidates a performance claim, even if an aggregate
metric is faster.

## SLO gates

After the correctness gate passes, the qualifying comparison must show all of:

| Metric | UniLume requirement |
| --- | --- |
| Browser scenario p50 and p95 completion time | At least 5% lower than Lotus in the paired median |
| Browser scenario throughput | At least 5% higher than Lotus in the paired median |
| Fcitx CPU time during scenarios | No more than Lotus, within the calibrated noise band |
| Fcitx RSS delta during the run | No more than Lotus, within the calibrated noise band |
| Core and deterministic integration benchmark | No regression against the recorded UniLume baseline |
| Long run | No linear RSS growth, no latency drift beyond the existing harness rule, and no correctness error |

The 5% margin is deliberately larger than a single scheduler tick or a typical
one-off desktop sample. #39 must record the measured noise band before calling
a near-boundary result a win. If the environment cannot distinguish a 5%
difference, the result is **inconclusive**, not a win.

For each `(round, scenario)` pair, the raw result records the candidate divided
by reference completion-time ratio. The harness reports the p50 and p95 of
those paired ratios; a value below `0.95` is the timing direction required by
the proposed 5% gate. Report the per-side distributions too, rather than
selecting the more favorable aggregate.

“Better than Lotus” means the gates above pass for every supported app/session
claim. It never means a core-only microbenchmark won while the desktop path
lost text or regressed.

## Browser harness

`scripts/benchmark/compare_fcitx5_desktop.py` performs the X11 browser part
of the protocol. It does not change Fcitx configuration. The operator must
first create an isolated Fcitx profile containing both input methods, open a
disposable browser profile at the probe page, and pass the exact Fcitx input
method names. The harness temporarily switches the active input method and
restores the previous method in a `finally` block.

It sends XTEST events through `xdotool`, waits for the application to expose
the resulting textarea value in the probe window title, and writes raw JSON.
Timing is therefore **scenario-completion latency**, including the event path,
Fcitx, and browser. It is not an engine-only per-key latency measurement.

Required host tools are `fcitx5`, `fcitx5-remote`, `xdotool`, `pgrep`, and a
native X11 browser. They are test-host requirements only; UniLume does not
depend on them at runtime.

Open the probe with a unique token (replace `TOKEN`):

```sh
google-chrome --user-data-dir="$(mktemp -d)" --no-first-run \
  "file://$PWD/tests/manual-apps/fcitx5-comparison-probe.html#TOKEN"
```

Find its X11 window id and run the paired measurement:

```sh
WINDOW_ID="$(xdotool search --name TOKEN | tail -n 1)"
python3 scripts/benchmark/compare_fcitx5_desktop.py \
  --candidate=UniLume \
  --reference='Vietnamese - Lotus' \
  --window-id="$WINDOW_ID" \
  --token=TOKEN \
  --method=telex \
  --rounds=5 \
  --seed=39 \
  --output=lotus-comparison-x11-chrome.json
```

Run `--preflight` first when setting up a machine. It performs no input-method
switch or keystroke injection. Use `--enforce-slo` for a qualifying run; the
harness then exits non-zero unless correctness, p50/p95/p99, throughput, CPU,
and RSS gates all pass. Result files are local evidence and must include the
corresponding UniLume and Lotus commit/package versions in the issue.

Telex, VNI, and VIQR must be measured separately with matching configurations.
The harness rejects an empty method corpus rather than silently comparing
different modes.

## Required evidence matrix

Run the protocol separately for each row. Do not combine measurements from
different rows.

| Session | Frontend | Minimum evidence |
| --- | --- | --- |
| X11 | Chromium/Chrome browser probe | Automated raw JSON, five paired rounds |
| X11 | Firefox browser probe | Automated raw JSON, five paired rounds |
| X11 | GTK and Qt native editors | Exact-output capture method plus raw timing/resource log |
| X11 | Electron/VSCode and terminal | Exact-output capture method plus raw timing/resource log |
| Wayland/KWin | Native GTK, Qt, Firefox, Chromium, Electron | Issue #58 protocol; no X11 harness substitution |
| Wayland/Mutter | Same | Issue #58 protocol |
| Wayland/wlroots | Same | Issue #58 protocol |

The browser harness is intentionally X11-only. Native Wayland comparison must
use the compositor-aware qualification work in #58; XTEST or XWayland is not
evidence for a native Wayland claim.

## Core and controller companion runs

Collect these on the same machine immediately before or after each desktop
pair. They diagnose regressions but do not replace the desktop comparison.

```sh
cmake -S . -B build/comparison \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNILUME_BUILD_BENCHMARKS=ON \
  -DUNILUME_BUILD_INTEGRATION_BENCHMARKS=ON
cmake --build build/comparison --parallel 2
ctest --test-dir build/comparison --output-on-failure
build/comparison/benchmarks/unilume_core_benchmark \
  --soak --keys=1000000 --format=json --output=unilume-core.json
build/comparison/benchmarks/unilume_integration_benchmark \
  --keys=1000000 --profile=all --format=json --output=unilume-integration.json
```

## Reporting

Attach raw JSON and these immutable facts to #39 or its release evidence:

- UniLume commit and Lotus package/commit;
- `uname -a`, `/etc/os-release`, CPU model, governor, RAM, Fcitx5 version;
- application version, session type, compositor/window system and frontend;
- exact Fcitx configuration exported from the disposable test profile;
- corpus SHA-256, seed, event delay, rounds, command lines and timestamps;
- failures, retries, exclusions, and every inconclusive result.

Never publish typed user text, an existing Fcitx profile, browser profile, or
secrets. Use disposable profiles and the fixed public corpus only.
