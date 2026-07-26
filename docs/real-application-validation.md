<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Real-application validation

This document records a controlled desktop validation of the experimental
Fcitx5 addon. It is evidence for the tested environment, not a claim that the
addon is production-ready on every Linux desktop.

## Environment

The 2026-07-24 validation used:

- Debian 13.6 (Trixie), Linux 6.12.95, KDE Plasma on X11;
- Fcitx5 5.1.12;
- GCC 14.2, Clang 19.1.7, and CMake 3.31.6;
- xterm 398, KWrite 25.04.3, Zenity 4.1.90, VSCode 1.129.1,
  Google Chrome 150.0.7871.114, and Firefox ESR 140.13.0.

Firefox was run from a temporary unpacked Debian package and Chrome, Firefox,
and VSCode used disposable test profiles where applicable. No system package
or existing input method was removed.

## User-local installation

Back up `~/.config/fcitx5` before changing the active input-method group.
Build and inspect a staging installation first:

```sh
cmake -S . -B build/real-app \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNILUME_BUILD_FCITX5_ADDON=ON \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build/real-app --parallel 2
ctest --test-dir build/real-app --output-on-failure

STAGE_DIR="$(mktemp -d)"
cmake --install build/real-app --prefix "$STAGE_DIR"
find "$STAGE_DIR" -type f -print
```

The tested Fcitx package did not search `$HOME/.local/lib/fcitx5`
automatically. A diagnostic launch used the documented addon search
environment rather than copying into `/usr`:

```sh
FCITX_DATA_HOME="$HOME/.local/share/fcitx5" \
FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5" \
fcitx5 -r
```

The exact system addon directory is distribution-specific. Packaging should
install to Fcitx's configured addon directory instead of relying on this
development override.

Rollback consists of selecting the previous input method, restoring the
backed-up Fcitx group configuration, restarting Fcitx, and removing only:

```text
~/.local/lib/fcitx5/unilume.so
~/.local/share/fcitx5/addon/unilume.conf
~/.local/share/fcitx5/inputmethod/unilume.conf
```

## Application results

| Application | Toolkit/path | Validation | Result |
| --- | --- | --- | --- |
| xterm | XIM terminal | controlled key replay | Pass using safe preedit fallback |
| KWrite | Qt | controlled replay plus cursor, Backspace, and focus changes | Pass; direct zero-preedit path |
| Zenity text entry | GTK4 | controlled key replay | Pass; direct zero-preedit path |
| VSCode editor | Electron | disposable profile, editor and save shortcut | Pass using client-preedit fallback |
| Chrome textarea/contenteditable/address bar | Blink | controlled local page and address-bar entry | Pass using client-preedit fallback |
| Firefox textarea/contenteditable/address bar | Gecko | controlled local page and address-bar entry | Pass after the mode-transition fix |

The corpus covered Telex composition, URL, email, C++/C#-like input, command
text, punctuation, Backspace, reset, cursor movement, control shortcuts, and
focus changes. The page probe exposes its complete textarea value in the
window title, so automation verifies output rather than assuming sent keys
were accepted. Firefox normalized one ordinary contenteditable boundary space
to a non-breaking space in the DOM while rendering the same visible text;
textarea and address-bar results remained exact.

Multi-byte UTF-8 passthrough is covered through the real engine API and
sanitizer suite. It was displayed successfully in controlled widgets, but
Japanese, Korean, Chinese, and emoji could not be generated as physical key
events from the ASCII-only automation tool. That part of the desktop matrix
is therefore semi-automated, not a claim of native keyboard-layout coverage.

## Direct commit and fallback

An input context chooses its path on the first processable key:

- a context that already exposes surrounding-text replacement starts on
  direct commit and has no client preedit underline;
- a context without that capability stays on client preedit for its lifetime;
- a direct context that loses the capability falls back and is not promoted
  again.

The policy intentionally never promotes an active preedit context to direct
replacement. Firefox demonstrated that its final preedit update can still be
pending when the first delete/commit request arrives. Crossing paths in that
window duplicated and re-composed prefixes. Keeping one path per context is
the safe behavior: it trades zero-preedit in unsupported frontends for
correct text.

Chrome visibly underlined client preedit even when the addon requested
`NoFlag`. Moving fallback to Fcitx server-side preedit removed the underline,
but a Firefox 1 ms/key burst then lost part of the Vietnamese sentence and the
URL prefix. That experiment was rejected: correct text takes priority over
appearance. Zero-preedit/no-underline is therefore verified only on the
direct path; browser/Electron fallback remains visibly composed and is a
known blocker for parity with established daily-use input methods.

## Diagnostic trace

Set `UNILUME_FCITX_DIAGNOSTICS=1` only for a controlled test session. Each
input context keeps a 64-event in-memory ring and emits it when the context is
destroyed. Entries contain an anonymous context number, sequence number,
handled/path state, delete count, commit/preedit byte lengths, queue/reset/
stale counters, reset reason, surrounding/cursor validity, and processing
duration. They do not contain committed text. The Release default performs no
per-key logging.

## Soak method and limitations

The repeatable release-candidate gate is
`scripts/benchmark/soak_fcitx5_desktop.py`. It verifies every probe result,
samples Fcitx RSS/high-water RSS, FD and thread counts, measures active and
idle CPU, checks checkpoint latency drift, and restarts Fcitx at a fixed
interval to exercise addon reload/reconnect. Qualifying mode refuses durations
below eight hours:

```sh
python3 scripts/benchmark/soak_fcitx5_desktop.py \
  --candidate unilume \
  --window-id "$PROBE_WINDOW" \
  --token "$PROBE_TOKEN" \
  --duration-hours 8 \
  --restart-interval-hours 2 \
  --output unilume-desktop-soak.json
```

A short `--smoke-seconds 60` run validates the harness but is explicitly
recorded as non-qualifying. Reports contain scenario identifiers and
correctness flags, never expected or observed typed text.

The real-application soak alternates focus between controlled Chrome and
Firefox textareas, replaces the complete corpus, verifies the exact resulting
title, and samples the Fcitx process RSS, high-water RSS, and CPU. RSS is
process-level because UniLume is an in-process addon; it is not an allocation
count for UniLume alone.

The accepted run lasted 1,800 seconds and completed 69 focus cycles
(138 verified application runs) with no output error. Fcitx RSS started at
66,688 KiB, reached a minimum of 63,856 KiB, changed once to 71,960 KiB near
the 20-minute point, and remained at 71,960 KiB for the final ten minutes.
This step-and-plateau pattern is not linear per-key growth, but it also cannot
be attributed to UniLume separately from Fcitx and its other addons. Reported
Fcitx CPU stayed at 0.4%. All but one frontend settle sample completed within
100 ms; one early Chrome sample took 1.4 seconds, with no latency growth in
later checkpoints.

The automation uses XTEST only after confirming the exact target window and
explicitly releases modifiers around `Ctrl+A`. Direct XSendEvent and uncleared
modifier variants were rejected during harness validation because Chrome can
drop direct synthetic events or interpret a stuck `Ctrl` plus `r` as reload.
Those harness failures are not counted in the accepted 30-minute run.

No desktop performance threshold is asserted. Results from this machine must
not be compared with another machine, Lotus, or fcitx5-unikey as if the
conditions were equivalent. Wayland, a native Qt terminal beyond the tested
KWrite widget, and a full 60-minute human work session remain broader
compatibility work.
