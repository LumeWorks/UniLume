<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Browser input policy

This document describes how UniLume selects the input path for browser and
Electron frontends and why some frontends show client-preedit underlines while
others use direct zero-preedit.

## Capability requirement

UniLume's direct zero-preedit path requires the Fcitx
`CapabilityFlag::SurroundingText` (bit 6) to be set by the frontend. This flag
signals that the application can provide the text around the cursor, letting the
input method delete and replace previously committed characters safely.

The following Fcitx 5.1.12 capability flags are relevant:

| Flag | Value | Purpose |
| --- | --- | --- |
| `SurroundingText` | 1 << 6 | Surrounding text available for replacement |
| `Preedit` | 1 << 1 | Frontend supports client-side preedit |

## Input path selection

The `InputModePolicy` state machine chooses one of three paths:

```
                    ┌──────────┐
     first key ────▶│  Unknown │◀── reset/focus
                    └────┬─────┘
                         │
              ┌──────────┴──────────┐
              │                     │
      SurroundingText         no SurroundingText
         available              (browser/Electron)
              │                     │
              ▼                     ▼
        ┌──────────┐         ┌──────────┐
        │  Direct  │         │ Preedit  │
        │ zero-    │         │ client   │
        │ preedit  │         │ fallback │
        └──────────┘         └──────────┘
              │                     │
              │   cap. lost         │
              ├────────────────────▶│
              │                     │
              │    composition      │
              │    reset/end        │
              │                     │
              └────► Unknown ◄──────┘
```

**Rules:**

1. Path is selected on the first key event of a composition based on the
   current `SurroundingText` capability.
2. During an active composition the path is stable:
   - `preedit` never promotes to `direct` (prevents overlapping preedit
     update from Firefox)
   - `direct` demotes to `preedit` only if `SurroundingText` is lost
3. Between compositions (after commit, reset, focus change, or navigation):
   - Both paths reset to `unknown`
   - The next composition re-evaluates the capability snapshot
4. A full `reset()` (focus change) always returns to `unknown`.

## Browser capability observations (X11 matrix)

The table below records real-application capability observations from
`docs/real-application-validation.md` on a single environment (KDE/X11,
Fcitx 5.1.12). These are empirical observations, not guarantees of behavior
on other configurations.

| Application | Toolkit | SurroundingText | Input path | Underline |
| --- | --- | --- | --- | --- |
| KWrite | Qt | Available on first key | Direct | None |
| Zenity text entry | GTK4 | Available on first key | Direct | None |
| xterm | XIM | Not available | Client preedit | Yes |
| Firefox textarea | Gecko | Not available | Client preedit | Yes |
| Firefox contenteditable | Gecko | Not available | Client preedit | Yes |
| Firefox address bar | Gecko | Not available | Client preedit | Yes |
| Chrome textarea | Blink | Not available | Client preedit | Yes |
| Chrome contenteditable | Blink | Not available | Client preedit | Yes |
| Chrome address bar | Blink | Not available | Client preedit | Yes |
| VSCode editor | Electron | Not available | Client preedit | Yes |
| VSCode integrated terminal | Electron | Not available | Client preedit | Yes |

**Key finding (Debian 13.6 / KDE Plasma / X11 matrix):** Firefox, Chrome, and
Electron/VS Code did not advertise `CapabilityFlag::SurroundingText` on any
context type observed in this environment. Those contexts remained on
client-preedit fallback for the duration of the test session. Other frontend
versions, desktop environments, or Wayland sessions may differ and remain
unverified.

## Why browsers stay on client preedit

The `CapabilityFlag::SurroundingText` is controlled by the application (via
Fcitx's input context protocol) or the IM module (XIM, Wayland text-input).
Browser engines:

- **Firefox:** Uses Gecko's own IM handling. In the tested X11 matrix, Firefox
  did not advertise `SurroundingText`. The GTK IM module used by Firefox on X11
  (`gtk-im-context`) can provide surrounding text only if the widget requests
  it; Firefox's input fields did not do so in this environment. Wayland may
  differ.
- **Chrome:** Uses Aura (Linux) or Ozone (Wayland). On the tested X11 via GTK,
  Chrome's renderer process handled IME independently of the browser window's
  GTK IM context, and `SurroundingText` was not advertised in this environment.
- **Electron/VS Code:** Inherits Chromium's IME architecture. Same observed
  behavior as Chrome in the tested environment.

This is not a UniLume limitation in the tested environment; the observed
behavior reflects the frontend API contract as exercised by these applications
on X11. Wayland, different browser versions, or alternative desktop
environments may produce different capability signals and would need separate
validation.

## Zero-preedit research

Three approaches were evaluated:

### A. Direct replacement (requires SurroundingText)

Direct replacement deletes the minimum characters before the cursor and commits
the replacement in one synchronous transaction. It requires valid, unselected
surrounding text with a trustworthy cursor position and enough characters before
the cursor.

**Browser applicability in tested X11 matrix:** In the environments tested,
browsers did not advertise `CapabilityFlag::SurroundingText`, so
`canReplace()` returned `false`. Whether Wayland or future browser versions
expose this capability is not yet known.

### B. Stable prefix commit

This approach commits the part of the preedit that is guaranteed not to change,
reducing the underlined portion. For example, a guessed prefix of `"t"` after
`"to"` looks stable until a later key rewrites earlier symbols.

**Analysis (Issue #24):** UniKey stores composition in `m_buffer[]` (composed
symbols + tone) and `m_keyStrokes[]` (replay). Earlier characters change due to
tone rewrite, roof/hook/`đ`, freeMarking, backspace tone moves, and
`autoNonVnRestore`. Longest common prefix of successive outputs is only an
observation, not a proof of immutability. A commit-only stable-prefix cannot
safely recall committed bytes on backspace without SurroundingText. A
forwarded-backspace architecture has not been evaluated.

Full research: [composition-span-research.md](composition-span-research.md).
Product summary: [stable-prefix.md](stable-prefix.md).
Counterexample locks:
`tests/engine/composition_span_counterexample_tests.cpp`.

**Verdict:** **Decision C (commit-only model)** — not safely implementable as a
mid-composition monotonic oracle for a commit-only design, without a deep
in-engine mutability API (or a forbidden second engine). Full client preedit
for the current word remains the safe browser path. No hybrid stable-prefix
feature flag is enabled. A forwarded-backspace architecture is outside this
research scope.

### C. Server-side preedit

Server-side preedit moves the preedit rendering from the client (browser) to
Fcitx, removing the underline. UniLume attempted this during previous
validation.

**Result in tested Firefox/X11 reproduction:** A Firefox 1 ms/key burst lost
part of the Vietnamese sentence and the URL prefix. The timing dependency
between server preedit updates and burst key events caused text loss that
could not be resolved without sleep or retry workarounds.

**Verdict:** Rejected for the tested Firefox/X11 reproduction. Server-side
preedit may behave differently on other frontends or compositors and has not
been re-evaluated.

### Summary

| Approach | Safe? | No underline? | Feasible in tested matrix? |
| --- | --- | --- | --- |
| A. Direct replacement | Yes | Yes (works for Qt/GTK) | No for browser contexts tested |
| B. Stable prefix | No (decision C) | Partial only if unsafe | Rejected mid-word without engine oracle |
| C. Server-side preedit | No (tested Firefox/X11) | Yes | Rejected for tested reproduction |

**Conclusion (tested X11 matrix):** Safe zero-preedit for browser/Electron
frontends is not currently achievable with the frontend capabilities observed
in the tested environment and the current UniKey engine interface. The
client-preedit fallback with underline is retained as the safe default for
all browser/Electron contexts in this matrix. Wayland and other environments
require separate evaluation.

## Deterministic test profiles

The integration harness models the following browser profiles:

| Profile | SurroundingText (observed) | Behavior |
| --- | --- | --- |
| `firefox-x11` | Not advertised in test matrix | Client-preedit fallback, burst-safe |
| `firefox-wayland` | Pending | Wayland session required |
| `chromium-x11` | Not advertised in test matrix | Client-preedit fallback, burst-safe |
| `chromium-wayland` | Pending | Wayland session required |
| `electron-x11` | Not advertised in test matrix | Client-preedit fallback, burst-safe |
| `electron-wayland` | Pending | Wayland session required |

Browser profiles are tested via `PreeditFallbackController` with the full
corpus (Telex composition, URL, email, C++/C# code, punctuation, backspace).
See `tests/integration/browser_capability_tests.cpp` and
`tests/integration/preedit_fallback_tests.cpp`.

## Limitations

- Browser/Electron capability observations are documented for Debian 13.6 / KDE
  Plasma X11 only. Other distributions, desktop environments, compositors, or
  Wayland sessions may differ and remain unverified.
- Wayland browser profiles are marked `pending`; see
  `docs/wayland-validation.md`.
- The policy never hardcodes process names to force a specific path.
- No claims are made about production readiness.
