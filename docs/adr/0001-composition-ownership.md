<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# ADR 0001: one composition owner with verified direct replacement

- Status: Accepted
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
   and cursor are valid, and cursor equals selection anchor;
2. `client preedit` in every other case.

The owner is immutable during an active composition. A capability loss, focus
change, cursor/selection invalidation, navigation boundary, or frontend
generation change crosses a reset barrier before another owner may start.
There is never a server-preedit, protocol helper, or uinput writer active
beside either owner.

This is a correctness decision, not a promise that every frontend has no
underline. Native Wayland can use the verified direct path when its live state
passes validation. Unsupported X11 browser/Electron contexts keep client
preedit.

## Transaction and recovery contract

| Event | Required behavior |
| --- | --- |
| First processable key | Snapshot capability, surrounding text, cursor, anchor, focus generation; choose one owner |
| Direct edit | Validate the same generation and replacement range immediately before delete/commit |
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
delete/commit/preedit state, but it does not manufacture surrounding text when
the application omits it.

## Options considered

| Option | Fault result | Latency/CPU/RSS | Portability, permission, maintenance | Decision |
| --- | --- | --- | --- | --- |
| Client preedit | Dropped/reordered visual snapshots do not change engine output; crash discards only uncommitted composition | In-process; test prototype processed 8.6M events in 6.004 s wall time on the evidence machine | Existing Fcitx frontend contract, no new permission or process, lowest maintenance | Selected fallback |
| Verified direct replacement | Blind replacement corrupts text after cursor/selection movement; snapshot validation and generation fencing prevent the edit | Lowest rendering overhead and no preedit; existing bounded transaction queue applies | Limited to frontends with validated state, no new permission, one maintained adapter | Selected only with a valid oracle |
| Server preedit | A late unacknowledged update can replace newer visible state; the real Firefox/X11 1 ms experiment lost text | Cosmetic path adds UI update work; rejected before a performance result could qualify it | Rendering semantics vary by frontend/compositor; another recovery contract to maintain | Rejected |
| Wayland input-method protocol | Only one input-method object is allowed per seat; a second UniLume owner conflicts with Fcitx | Batching is useful but already paid for in the Fcitx frontend; no separate eligible RSS result | Compositor-specific and single-seat-owner contract; no extra permission when used through Fcitx | Use through Fcitx, not as another backend |
| Helper/uinput | A focus change sends delete/insert keys to the wrong application; selection and cursor cannot be verified | Another process/device and IPC would add CPU/RSS; rejected before optimization | Linux-only, requires `/dev/uinput` access, largest security and maintenance surface | Rejected |

The timing number is a prototype comparison floor, not end-to-end desktop
latency. Server-preedit, a second Wayland owner, and uinput are rejected on
correctness/ownership before performance can make them eligible.

## Preliminary threat model

Protected assets are user text, target focus, selection, clipboard secrecy,
and input integrity.

- A helper with uinput access can synthesize keys system-wide. UniLume will not
  request that permission.
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

No rollback may enable server-preedit or uinput implicitly.

## Consequences

- Correct text wins over removing the underline.
- Native Wayland is eligible for zero-preedit based on capability and live
  state, not an application allowlist.
- X11 browser/Electron frontends observed without `SurroundingText` remain
  client-preedit.
- Issue #48 may harden this existing state machine and recovery contract. It
  must not introduce a second composition writer.
- Issue #49's helper/uinput path is unnecessary under this decision unless a
  later ADR supplies a new oracle and threat model.
