<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Wayland validation

This document describes the Wayland validation status for the UniLume Fcitx5
addon. It records what has been measured automatically, and provides a
checklist for the compositors that cannot be driven from inside a session.

## Current status

**wlroots, KWin and Mutter are measured automatically.**

An automated harness now qualifies a native Wayland session by injecting real
key events and reading the exact bytes a real Wayland client received. The
current evidence is:

| Compositor family | Compositor | Evidence | Verdict |
| --- | --- | --- | --- |
| wlroots | sway 1.9 (headless backend) | native terminal, exact output | corpus, 1 ms burst and unpaced stress pass |
| KWin | KWin 6.3.6 (nested X11 backend) | native GTK3, exact output | direct corpus and 1 ms burst pass |
| Mutter | GNOME Shell/Mutter 48.7 (headless virtual monitor) | native GTK3, exact output | direct corpus and 1 ms burst pass |

Earlier work under #47 ran real Firefox, Chrome and VS Code/Electron Wayland
clients inside an isolated KWin 6.3.6 virtual compositor with Fcitx 5.1.12, and
all three advertised `SurroundingText`; see `zero-preedit-evidence.md`. That
remains a capability trace, not a key/output result.

All Wayland-related compile paths are verified through the CI matrix. A compile
check is never accepted as runtime validation.

## Automated qualification

```sh
cmake -S . -B build/fcitx5 \
  -DCMAKE_BUILD_TYPE=Release -DUNILUME_BUILD_FCITX5_ADDON=ON
cmake --build build/fcitx5
cmake --install build/fcitx5 --prefix /tmp/ul-prefix

./scripts/test/wayland_qualification_session.sh /tmp/ul-prefix \
  --burst-rounds 5 --stress-rounds 3 --soak-seconds 120 \
  --output /tmp/wlroots-sway.json
```

`scripts/test/wayland_qualification_session.sh` builds a disposable wlroots
session: a headless sway compositor, a private D-Bus session, a private Fcitx
profile and a private addon directory. It never touches the operator's desktop.
`scripts/test/nested_wayland_qualification_session.sh` does the same for KWin
and Mutter:

```sh
./scripts/test/nested_wayland_qualification_session.sh \
  /tmp/ul-prefix kwin --burst-rounds 5 --output /tmp/kwin.json
./scripts/test/nested_wayland_qualification_session.sh \
  /tmp/ul-prefix mutter --burst-rounds 5 --output /tmp/mutter.json
```

To qualify a compositor you are already running, invoke
`scripts/test/qualify_wayland_compositor.py` directly inside that session.
The same harness can launch a browser with `--client browser-probe --browser
google-chrome`. It removes `DISPLAY` from the browser environment, forces the
native Wayland Chromium IME path, and obtains live and committed textarea
values through a token-authenticated loopback endpoint. A visual inspection or
window title is not accepted as output evidence.

The pure decision logic has unit coverage that needs no compositor:

```sh
python3 -B tests/wayland/qualify_wayland_compositor_test.py
```

### How the harness produces evidence

> [!NOTE]
> **Injection.** Key events are delivered through `zwp_virtual_keyboard_v1`
> using `wtype` on wlroots. A nested KWin session receives XTEST at its outer
> compositor window, then delivers normal seat events to its native Wayland
> clients. Mutter receives keysyms through its compositor-owned
> `org.gnome.Mutter.RemoteDesktop.Session`. Neither nested path is a UniLume
> backend, and neither uses `uinput`.

> [!NOTE]
> **Extraction.** On wlroots, a `foot` terminal runs `cat` with the pty in
> `-icanon -echo`,
> so the kernel line discipline neither echoes nor erases. A Backspace that
> UniLume failed to consume therefore surfaces as a literal delete byte and
> fails the comparison instead of being silently absorbed by the terminal.
> KWin and Mutter use a native GTK3 entry probe that records its live state
> before Return and its exact committed UTF-8 after Return.

The harness refuses to report a result unless the session is really native
Wayland, and it verifies that the engine actually transforms keystrokes before
measuring anything. Selecting the input method is not sufficient evidence,
because raw passthrough reproduces plain ASCII unchanged.

### Phases and what each one gates

| Phase | Key rate | Gates the verdict |
| --- | --- | --- |
| `corpus` | 10 ms/key | yes, exact output |
| `burst` | 1 ms/key, the rate this document specifies | yes, no lost, duplicate or reordered text |
| `stress` | no delay at all | no, reported as findings only |
| `soak` | 10 ms/key for a fixed duration | yes, correctness and bounded resources |

The `stress` phase deliberately exceeds any human typing rate, so its defects
are recorded but do not set the verdict. Keeping it separate stops an
unbounded-speed artifact from being reported as a gate failure, and stops a
real defect from hiding behind a lenient gate.

### Direct replacement against preedit fallback

Each scenario is sampled twice: once before any commit boundary is sent, and
once after. Text already present in the first sample proves UniLume replaced
the composition directly. Text that appears only after the boundary means the
run observed the preedit fallback. The harness then cross-checks that
client-observed path against the path the addon's own diagnostic trace claims,
so the addon cannot report a backend it did not actually use.

## Known limitations of this environment

1. **`uinput` is not used.** Issue #58 places it out of scope because a
   kernel-level injection path would mask native-path behaviour, so the harness
   fails loudly instead of substituting it.
2. **Terminal clients do not provide surrounding text.** With `foot`, the
   direct replacement path is correctly ineligible and the diagnostic trace
   records the `unavailable` capability gate. The GTK probe provides validated
   surrounding text and qualifies direct replacement separately.
3. **One run claims one compositor and client.** The evidence JSON records the
   exact family, version, injector and extraction client. It is not evidence
   for an untested application or compositor version.

## Environment check script

Run the following to determine whether the current session is X11, XWayland,
or native Wayland:

```sh
#!/bin/sh
# wayland-check.sh — determine display server and Fcitx5 environment
echo "Session type: ${XDG_SESSION_TYPE:-unknown}"
echo "Desktop:      ${XDG_CURRENT_DESKTOP:-unknown}"
echo "Wayland display: ${WAYLAND_DISPLAY:-none}"
echo "X11 display:     ${DISPLAY:-none}"
echo "Compositor:      ${XDG_SESSION_DESKTOP:-unknown}"
echo "Fcitx5 version:  $(fcitx5 --version 2>/dev/null || echo not found)"
```

## Manual validation checklist

This checklist covers graphical application families beyond the controlled
GTK probe. Where a case is already covered automatically, prefer the harness,
because it compares exact output rather than appearance.

Each item below must be tested on a native Wayland session (not XWayland).
The tester should use the user-local install procedure from
`docs/real-application-validation.md`.

### 1. Environment

- [ ] Distro and version recorded
- [ ] Compositor (KWin, Mutter, Weston, etc.) and version recorded
- [ ] Fcitx5 version recorded
- [ ] Fcitx5 frontend/backend (`fcitx5-diagnose` output saved)
- [ ] Browser versions recorded
- [ ] `XDG_SESSION_TYPE=wayland` confirmed

### 2. Session type verification

- [ ] `echo $WAYLAND_DISPLAY` returns non-empty
- [ ] `echo $DISPLAY` is empty or points to XWayland (usually `:0`)
- [ ] `xdg-desktop-portal` reports Wayland
- [ ] GTK applications use Wayland (`GDK_BACKEND=wayland` or auto-detected)
- [ ] Qt applications use Wayland (`QT_QPA_PLATFORM=wayland` or auto-detected)

### 3. Native Wayland applications

- [ ] Native Wayland GTK application (e.g., `GDK_BACKEND=wayland gedit`)
  - [ ] Direct zero-preedit (no underline)
  - [ ] Telex composition in textarea
  - [ ] Backspace
  - [ ] Cursor movement
  - [ ] Focus change (tab away and back)
  - [ ] CTRL shortcut does not lose text
  - [ ] URL passthrough (http://abc.com/a1)
  - [ ] Email passthrough (user@example.com)
  - [ ] Unicode passthrough (日本語 한국어 中文 🙂🚀)
- [ ] Native Wayland Qt application (e.g., `QT_QPA_PLATFORM=wayland kate`)
  - [ ] Direct zero-preedit (no underline)
  - [ ] Same cases as GTK above

### 4. Firefox Wayland

Firefox auto-detects Wayland on `MOZ_ENABLE_WAYLAND=1` or when
`GDK_BACKEND=wayland` is set in newer versions. To force Wayland:

```sh
MOZ_ENABLE_WAYLAND=1 firefox --new-instance --profile /tmp/firefox-wayland-test
```

- [ ] Firefox reports Wayland (`about:support` → "Window Protocol" shows "wayland")
- [ ] Textarea: Telex composition works
- [ ] Textarea: backspace works
- [ ] Textarea: cursor movement works
- [ ] Textarea: URL/email passthrough
- [ ] Contenteditable (`<div contenteditable>`): same cases
- [ ] Address bar: Telex composition
- [ ] Address bar: URL entry does not get Vietnamese marks
- [ ] Focus switch between tabs
- [ ] CTRL+S / CTRL+T shortcut does not lose text
- [ ] Burst (type 100+ characters rapidly) — no lost/duplicate/reordered
- [ ] No freeze or crash during 5+ minutes of use

### 5. Chromium/Chrome Wayland

Chromium auto-detects Wayland on `--ozone-platform-hint=auto` or can be forced:

```sh
google-chrome --ozone-platform=wayland --user-data-dir=/tmp/chrome-wayland-test
```

- [ ] Chrome reports Wayland (`chrome://gpu` shows "Wayland")
- [ ] Same cases as Firefox (textarea, contenteditable, address bar)
- [ ] Burst test
- [ ] Focus switch

### 6. VSCode/Electron Wayland

VS Code auto-detects Wayland on `--ozone-platform-hint=auto`. To force:

```sh
code --ozone-platform=wayland --user-data-dir=/tmp/code-wayland-test
```

- [ ] VSCode editor: Telex composition
- [ ] VSCode integrated terminal: Telex composition
- [ ] VSCode search widget: composition works
- [ ] VSCode command palette: composition works
- [ ] CTRL+S save does not lose text
- [ ] Focus switch between editor and terminal

### 7. XWayland applications

Applications running through XWayland (legacy X11 apps on a Wayland session):

- [ ] xterm or X11 terminal
- [ ] Older GTK2 applications
- [ ] Same corpus as X11 validation

### 8. Key scenarios

Pass/fail for each on native Wayland:

| Scenario | Firefox | Chrome | VSCode/Electron | GTK | Qt |
| --- | --- | --- | --- | --- | --- |
| SurroundingText capability | | | | | |
| Direct zero-preedit | | | | | |
| Client preedit fallback | | | | | |
| Lost text on burst (1 ms/key) | | | | | |
| Duplicate text | | | | | |
| Reordered text | | | | | |
| Backspace correctness | | | | | |
| Focus change resets state | | | | | |
| Control shortcut safety | | | | | |
| URL passthrough | | | | | |
| Email passthrough | | | | | |
| Code-like input | | | | | |
| Unicode passthrough | | | | | |

### 9. Burst test procedure

1. Open a textarea on a local HTML page
2. Prepare `tooi ddang gox tieengs Vieetj http://abc.com/a1 user@example.com`
3. Paste the entire sentence at once or type at maximum speed
4. Verify the output matches `tôi đang gõ tiếng Việt http://abc.com/a1 user@example.com`
5. Repeat 10 times
6. No lost/duplicate/reordered characters

### 10. Soak

- [ ] Typing for 30+ minutes in mixed applications
- [ ] Fcitx5 process RSS stable (no unbounded growth)
- [ ] No crashes or freezes
- [ ] Output verified at end of soak

## Measured results

### wlroots, sway 1.9, native Wayland

Recorded with `scripts/test/wayland_qualification_session.sh` after the
transactional preedit fix in #86. Environment: Ubuntu 24.04.4,
Fcitx 5.1.7 `waylandim`, foot 1.16.2 `+ime`, no XWayland, Telex.

| Phase | Observations | Errors | Defects |
| --- | --- | --- | --- |
| `corpus` (10 ms/key) | 6 | 0 | none |
| `burst` (1 ms/key) | 18 | 0 | none |
| `stress` (no delay) | 12 | 0 | none |

Backend path: the client observed the **preedit fallback**, and the addon's
diagnostic trace independently reported `preedit`, so the two agree. The
capability gate `unavailable` was recorded 64 times, which is correct because a
terminal does not provide surrounding text.

The client and diagnostic trace both reported `preedit`. There were zero
backend failures. The earlier lost-segment reproduction is retained in #86;
its real wlroots retest now passes and the CI job is no longer
`continue-on-error`.

### KWin 6.3.6 and Mutter 48.7, native Wayland GTK3

Both isolated Debian 13.6 runs used Fcitx 5.1.12, the same six-scenario corpus,
the native GTK3 exact-output probe, and capability-gated direct replacement.

| Family | Phase | Observations | Errors | Defects | Observed path |
| --- | --- | ---: | ---: | --- | --- |
| KWin | corpus (10 ms/key) | 6 | 0 | none | direct |
| KWin | burst (1 ms/key) | 6 | 0 | none | direct |
| Mutter | corpus (10 ms/key) | 6 | 0 | none | direct |
| Mutter | burst (1 ms/key) | 6 | 0 | none | direct |

For every row the client-observed path and UniLume diagnostic path agree.
There were no backend failures, stale results, uncertain outcomes, lost
characters, duplicates or reordered output.

### Chromium native Wayland blocker

The controlled browser probe found a blocking direct-path defect on Google
Chrome 150.0.7871.114 under the same Debian 13.6 / KWin 6.3.6 / Fcitx 5.1.12
environment. At 10 ms/key, five of six scenarios were exact and the Backspace
scenario produced `tiếng` instead of `tiến`. At 1 ms/key, only one of six
scenarios was exact; examples include `tiếng Vieệt` instead of `tiếng Việt`
and `asf` instead of `à`. The diagnostic bundle recorded the direct path with
zero backend failures, stale results or uncertain outcomes, so this is not
classified as an injector or browser-extraction failure.

The root fix and real-browser regression are tracked by #90. Issue #58 remains
open until that blocker passes and Firefox/Electron/Qt coverage is complete.

The first automated 30-minute direct-path soaks also found one corrupted
observation among 15,801 on KWin and four among 9,674 on Mutter, despite clean
corpus, burst and stress phases and stable RSS/thread counts. The wlroots
preedit-path soak passed. The long-run direct blocker is tracked by #91.
Qualification reports now retain a bounded list of exact soak failures instead
of only an aggregate count, so its retest can identify the concrete sequence
without allowing an unbounded artifact.

## Implementation gaps (Wayland)

If the tester encounters any of the following, record the environment and skip
the affected test case:

1. `CapabilityFlag::SurroundingText` behavior differs from X11
2. `deleteSurroundingText()` transport or surrounding-state behavior differs
   across the compositor/application protocol combination
3. Client preedit not supported (application does not call
   `input_method_request` or `set_surrounding_text`)
4. Server preedit not supported by compositor
5. Fcitx5 input-method window protocol issues
6. Compositor-specific text-input protocol version incompatibility

## Links

- [Fcitx5 Wayland documentation](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)
- [wlroots text-input protocol](https://gitlab.freedesktop.org/wlroots/wlr-protocols)
- [KDE Wayland status](https://community.kde.org/KDE_Wayland_Status)
