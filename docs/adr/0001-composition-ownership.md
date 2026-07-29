<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# ADR 0001: one composition owner with verified direct replacement

- Status: Accepted, amended by Issue #102 on 2026-07-29
- Date: 2026-07-26
- Issue: [#47](https://github.com/dismonjames/UniLume/issues/47)
- Supersedes: no previous production decision

## Context

UniLume can rewrite an already emitted Vietnamese word only when the frontend
provides a trustworthy view of the text, cursor, and selection. Issue #24
proved that the UniKey output stream has no safe monotonic mid-word prefix
that can replace this observation.

Real capability traces in
[zero-preedit-evidence.md](../zero-preedit-evidence.md) show two distinct
contracts:

- Firefox, Chrome, and VS Code/Electron on the tested X11 path advertise
  `0x6000000032`, without `SurroundingText`;
- the same applications on native Wayland under KWin advertise `0x72` or
  `0x80072`, including `SurroundingText` (`0x40`).

The design therefore cannot promise zero-preedit based on an application name.
It must select a path from the current input-context capability and validated
state.

## Decision

Use exactly one capability-gated composition state machine:

```text
                           valid surrounding snapshot
                    ┌──────────────────────────────────┐
                    │                                  ▼
reset/focus ──▶ unknown                         verified direct
                    │                                  │
                    │ no valid snapshot                │ capability/state loss
                    ▼                                  ▼
              client preedit ◀──── reset barrier ── no owner
```

`InputContextState` is the sole owner. A composition receives one owner:

1. `verified direct` only when `SurroundingText` is present, the UTF-8 state
   and cursor are valid, cursor equals selection anchor, and the frontend can
   apply delete plus commit as one indivisible edit;
2. `acknowledged direct` on known split Fcitx transports when the bounded
   Backspace device is available;
3. `client preedit` in every other case.

The owner is immutable during an active composition. A capability loss, focus
change, cursor/selection invalidation, navigation boundary, or frontend
generation change crosses a reset barrier before another owner may start.
There is never a server-preedit or second protocol composition owner. The
acknowledged transport may emit deletion-only Backspaces, but
`InputContextState` remains the only owner of replacement text and commit.

This is a correctness decision, not a promise that every frontend has no
underline. Native Wayland can use the verified direct path when its live state
passes validation. Unsupported X11 browser/Electron contexts keep client
preedit.

## Transaction and recovery contract

| Event | Required behavior |
| --- | --- |
| First processable key | Snapshot capability, surrounding text, cursor, anchor, focus generation; choose one owner |
| Direct edit | Validate the same generation, replacement range and atomic frontend transport immediately before delete/commit |
| Capability/cursor/selection loss | Abort/reset direct state; cross a barrier; do not replay or guess an edit |
| Client-preedit visual update lost | Keep engine state authoritative; a missing visual snapshot cannot mutate committed text |
| Visual update reordered/duplicated | Ignore a stale sequence; snapshots are complete, not deltas |
| Focus/navigation/control boundary | Commit only through the active owner's documented boundary behavior, then reset |
| Frontend crash/disconnect | Discard uncommitted preedit and invalidate the generation; do not inject it into another target |
| Reconnect | Start with no owner and a new generation |
| Delayed direct completion | Ignore completion from an older generation/sequence |

The Fcitx Wayland frontend remains the protocol owner. UniLume is an input
method engine inside Fcitx and must not bind a second
`zwp_input_method_v2` object. The protocol can batch
delete/commit/preedit state, but Fcitx 5.1.12's public addon API does not expose
that batch: its Wayland v2 frontend flushes addon delete and commit requests
separately. Issue #90 therefore makes `wayland` and `wayland_v2` fail the
atomic-transport gate and use client preedit. This preserves the original
single-owner and correctness-first decision without adding another backend.
Issue #91 extends the same atomic gate to Fcitx's asynchronous `dbus`
frontend. Issue #102 then adds an ordered alternative for these split
transports: one Backspace is emitted, its press and release return through
Fcitx, and only then is the next deletion emitted. A final filtered Backspace
is the commit barrier. There are no timing sleeps or application-name rules.

## Options considered

| Option | Fault result | Latency/CPU/RSS | Portability, permission, maintenance | Decision |
| --- | --- | --- | --- | --- |
| Client preedit | Dropped/reordered visual snapshots do not change engine output; crash discards only uncommitted composition | In-process; test prototype processed 8.6M events in 6.004 s wall time on the evidence machine | Existing Fcitx frontend contract, no new permission or process, lowest maintenance | Selected fallback |
| Verified direct replacement | Blind replacement corrupts text after cursor/selection movement; snapshot validation and generation fencing prevent the edit | Lowest rendering overhead and no preedit; existing bounded transaction queue applies | Limited to frontends with validated state, no new permission, one maintained adapter | Selected only with a valid oracle |
| Server preedit | A late unacknowledged update can replace newer visible state; the real Firefox/X11 1 ms experiment lost text | Cosmetic path adds UI update work; rejected before a performance result could qualify it | Rendering semantics vary by frontend/compositor; another recovery contract to maintain | Rejected |
| Wayland input-method protocol | Only one input-method object is allowed per seat; a second UniLume owner conflicts with Fcitx | Batching is useful but already paid for in the Fcitx frontend; no separate eligible RSS result | Compositor-specific and single-seat-owner contract; no extra permission when used through Fcitx | Use through Fcitx, not as another backend |
| Acknowledged uinput deletion | A focus change can retarget unacknowledged keys; one-at-a-time emission, input-context ACK, bounded queue and final barrier constrain the window | One in-process device; no helper, socket, sleep or polling loop | Linux-only and requires active-session `/dev/uinput` access | Selected for split Fcitx transports by Issue #102 |

The timing number is a prototype comparison floor, not end-to-end desktop
latency. Server-preedit and a second Wayland owner remain rejected.

## Preliminary threat model

Protected assets are user text, target focus, selection, clipboard secrecy,
and input integrity.

- The uinput device exposes only `KEY_BACKSPACE`, emits only while one bounded
  transaction is active, and is owned by the Fcitx addon rather than a helper.
- Stale input-context state can delete unrelated text. Direct replacement is
  denied unless the live state validates, and stale generations are ignored.
- A malicious or broken frontend can advertise false capability. Range and
  UTF-8 validation fail closed to client preedit/reset; process names are not
  trusted.
- Diagnostic traces contain sizes, flags, timings, and anonymous context
  numbers only. They must never contain typed text.
- Password/sensitive contexts must continue to follow Fcitx capability policy;
  this ADR grants no new capture or persistence.

## Rollback

The prototype is test-only and has no production switch. If the follow-up
implementation violates an invariant:

1. disable only the new compatibility/direct hardening path;
2. retain client preedit as the fail-closed path;
3. reset all contexts/generations;
4. restore the previous UniLume package or select the prior input method using
   the existing user-local rollback procedure.

Setting `VerifiedDirectEnabled=False` disables both direct transports and
returns every context to safe preedit.

## Consequences

- Correct text wins over removing the underline.
- A native Wayland frontend is eligible for zero-preedit only when capability,
  live state and atomic transport all validate, not from an application
  allowlist. Fcitx 5.1.12's Wayland addon transport does not qualify.
- X11 browser/Electron frontends observed without `SurroundingText` remain
  client-preedit.
- Issue #48 may harden this existing state machine and recovery contract. It
  must not introduce a second composition writer.
- Issue #102 supplies the bounded acknowledged transport amendment; it does
  not authorize arbitrary key injection, mouse monitoring or a helper daemon.
