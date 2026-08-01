<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# ADR 0005: Direct-only Fcitx composition runtime

Status: Superseded for `automatic` by ADR 0006. Retained for explicit
`direct` compatibility mode.

## Decision

The production Fcitx addon exposes Direct and Off only. `InputContextState`
does not construct or invoke `PreeditFallbackController` and does not call
Fcitx preedit/InputPanel APIs for Vietnamese composition. The core preedit
implementation remains solely as compatibility and historical test material.

Atomic transports retain verified surrounding-text replacement. Split D-Bus
and Wayland transports use a single in-process Backspace-only uinput sequence:
the requested deletion count plus one consumed sentinel, with at most one key
pair in flight. The first pair waits for the matching physical triggering key
release. `DirectStrategy=Guarded` is the default, commits on sentinel release,
and validates surrounding state when a refreshed snapshot exists.
Fast is an opt-in sentinel-press boundary.

Direct backend absence or pre-dispatch failure is raw event passthrough.
Post-dispatch uncertainty never triggers another commit and fences Direct until
reset. Modified shortcuts and non-text controls are classified before mode
synchronization and are never filtered or queued.

## Consequences

UniLume no longer guarantees Vietnamese composition where neither direct
transport is available; Off-style passthrough is preferable to any underline
or hidden preedit ownership. Guarded favors ordering and Fast is the explicit
lower-latency boundary. The design adds no daemon, timer, retry loop, thread,
application allowlist, or second text engine.

This ADR supersedes ADR 0001's client-preedit production fallback. ADR 0001
remains as the historical ownership analysis that led to the earlier design.
