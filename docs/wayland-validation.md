<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Wayland validation

This document describes the Wayland validation status for the UniLume Fcitx5
addon and provides a checklist for manual testing on native Wayland sessions.

## Current status

**Capability trace complete; interactive matrix pending.** Issue #47 ran real
Firefox, Chrome, and VS Code/Electron Wayland clients inside an isolated KWin
6.3.6 virtual compositor with Fcitx 5.1.12. All three advertised
`SurroundingText`; see `zero-preedit-evidence.md`. The primary desktop remains
X11, so the full interactive checklist below is not yet claimed.

All Wayland-related compile paths are verified through the CI matrix. The
runtime evidence currently covers native browser capability negotiation in an
isolated compositor, not the complete key/output checklist.

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
