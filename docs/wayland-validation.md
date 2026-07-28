<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Wayland validation

This document describes the Wayland validation status for the UniLume Fcitx5
addon. It records what has been measured automatically, and provides a
checklist for the compositors that cannot be driven from inside a session.

## Current status

**wlroots measured automatically; KWin and Mutter still interactive.**

An automated harness now qualifies a native Wayland session by injecting real
key events and reading the exact bytes a real Wayland client received. The
current evidence is:

| Compositor family | Compositor | Evidence | Verdict |
| --- | --- | --- | --- |
| wlroots | sway 1.9 (headless backend) | automated, exact output | corpus passes; burst blocked by [#86](https://github.com/dismonjames/UniLume/issues/86) |
| KWin | — | not runnable from inside a session | not claimed |
| Mutter | — | not runnable from inside a session | not claimed |

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
To qualify a compositor you are already running, invoke
`scripts/test/qualify_wayland_compositor.py` directly inside that session.

The pure decision logic has unit coverage that needs no compositor:

```sh
python3 -B tests/wayland/qualify_wayland_compositor_test.py
```

### How the harness produces evidence

> [!NOTE]
> **Injection.** Key events are delivered through `zwp_virtual_keyboard_v1`
> using `wtype`. Only compositors that implement that protocol can be driven
> from inside the session.

> [!NOTE]
> **Extraction.** A `foot` terminal runs `cat` with the pty in `-icanon -echo`,
> so the kernel line discipline neither echoes nor erases. A Backspace that
> UniLume failed to consume therefore surfaces as a literal delete byte and
> fails the comparison instead of being silently absorbed by the terminal.

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

1. **KWin and Mutter cannot be qualified from inside a session.** Neither
   implements `zwp_virtual_keyboard_v1`, deliberately, so injected key events
   have no transport. They must be driven at the kernel level or tested
   interactively.
2. **`uinput` is not used.** Issue #58 places it out of scope because a
   kernel-level injection path would mask native-path behaviour, so the harness
   fails loudly instead of substituting it.
3. **Terminal clients do not provide surrounding text.** With `foot`, the
   direct replacement path is correctly ineligible and the diagnostic trace
   records the `unavailable` capability gate. Qualifying the zero-preedit path
   itself needs a client that provides surrounding text.

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

This checklist covers what the automated harness cannot reach: the KWin and
Mutter families, and the graphical applications that provide surrounding text
and therefore exercise the direct zero-preedit path. Where a case is already
covered automatically for wlroots, prefer the harness, because it compares exact
output rather than appearance.

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

Recorded with `scripts/test/wayland_qualification_session.sh` at UniLume
`d353d423e024c27a95c347af09bbf7638bfa9ae3`. Environment: Ubuntu 24.04.4,
Fcitx 5.1.7 `waylandim`, foot 1.16.2 `+ime`, no XWayland, Telex.

| Phase | Observations | Errors | Defects |
| --- | --- | --- | --- |
| `corpus` (10 ms/key) | 6 | 0 | none |
| `burst` (1 ms/key) | 30 | 2 | 2 lost |
| `stress` (no delay) | 18 | 0 | none |
| `soak` (120 s) | 154 | 0 | none |

Backend path: the client observed the **preedit fallback**, and the addon's
diagnostic trace independently reported `preedit`, so the two agree. The
capability gate `unavailable` was recorded 64 times, which is correct because a
terminal does not provide surrounding text.

Resource behaviour over the soak was clean: RSS 33780 KiB at both the first and
last sample, zero growth, thread count stable at 4, and no backend failures.

The soak is recorded as **non-qualifying** because this document requires at
least 30 minutes and the run was 120 seconds.

### Blocking finding

The `burst` phase failed its gate. Two of 30 observations lost one already
committed word, twice identically:

```text
scenario: natural_phrase
expected: 'tôi đang gõ tiếng việt'
observed: 'đang gõ tiếng việt'
```

With the input method switched off, the same corpus injected at unbounded speed
arrived intact in five consecutive runs, so the loss is not a virtual-keyboard
or compositor transport artifact. Every incident counter in the diagnostic
bundle nevertheless stayed at zero across 4105 events, so the current
diagnostics cannot attribute this loss class.

Tracked as [#86](https://github.com/dismonjames/UniLume/issues/86) and
deliberately not fixed alongside the harness.

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
