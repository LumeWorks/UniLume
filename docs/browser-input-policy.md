<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Browser and split-transport input policy

Issue #107 replaces the former preedit fallback policy. Vietnamese composition
in the production Fcitx runtime now has only Direct and Off ownership; it never
calls Fcitx preedit or InputPanel APIs.

## Backend selection

Atomic frontends keep verified surrounding-text replacement: the snapshot must
be valid UTF-8, bounded to 64 KiB, have a collapsed cursor, and contain the
requested deletion span.

Fcitx `dbus`, `wayland`, and `wayland_v2` are split transports. When the
Backspace-only uinput device is available, UniLume dispatches the required
deletion Backspaces plus one sentinel after the physical triggering key release,
with at most one synthetic key pair in flight.

- `Fast` (default) commits replacement text when the sentinel press returns.
- `Guarded` is opt-in and commits on sentinel release and checks a refreshed surrounding
  cursor/text snapshot when the frontend has published one.

There is no sleep, timer, retry loop, helper daemon, worker thread, application
allowlist, or second composition owner. Fast is an ordering boundary in the
same input context, not a claim that separate frontend messages are atomic.

If no direct backend is eligible, Direct behaves as raw passthrough. It does
not use client preedit, server preedit, or a synthetic raw commit. A partial
uinput dispatch or loss after dispatch is uncertain: no replacement is
committed, the generation is fenced, and the context stays in passthrough
until reset.

## Shortcut boundary

Before capability observation or engine submission, the addon passes through
all Ctrl/Alt/Super/Meta/Hyper/AltGr combinations, Tab and Shift+Tab,
ISO_Left_Tab, cursor/navigation keys, Delete, Escape, and other non-text keys.
They only fence local state. Enter performs the line boundary reset and passes
through. Plain Backspace remains Vietnamese engine input; Shift plus a
printable character remains available for capitalization.

## Scope of evidence

Earlier documents and validation logs may record client-preedit fallback; they
are historical evidence, not the current product contract. Automated tests
cover Direct/Off policy, raw failure passthrough, sentinel ordering, and
shortcut classification with Fcitx key types. Native KWin Wayland and the full
X11 application matrix still require the real-session checklist in
[wayland-validation.md](wayland-validation.md) and
[real-application-validation.md](real-application-validation.md).
