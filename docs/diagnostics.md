<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Production diagnostics

UniLume diagnostics are disabled by default. The facility is a per-input-
context, event-loop-owned ring for diagnosing input-path and backend state. It
does not send telemetry, open a network connection, or perform file I/O while
processing a key.

## Privacy inventory

The retained and exported fields are deliberately closed:

| Included | Never accepted or retained |
| --- | --- |
| monotonic diagnostic/backend sequence | typed or committed text |
| direct/preedit/off path | preedit or surrounding text |
| closed capability state | clipboard or selection content |
| queue depth, reset reason and preedit handoff count | application/window identity |
| stale, uncertain, fallback and backend-failure counters | file contents, config values or hotkeys |
| coarse duration bucket | hostname, username or environment dump |
| UniLume/Fcitx version, kernel release and closed session type | arbitrary metadata strings |

There is no text field in the event structure. Commit, preedit and surrounding
lengths are also omitted so a diagnostic bundle does not reveal word shapes.
Version and kernel tokens use a short allowlist; `XDG_SESSION_TYPE` is mapped
only to `x11`, `wayland`, `tty` or `unknown`.

## Enable and export

Enable structured journal output only for a reproduction:

```sh
UNILUME_FCITX_DIAGNOSTICS=1 fcitx5 -rd
```

To also write a private diagnostic bundle, provide an explicit destination:

```sh
UNILUME_FCITX_DIAGNOSTICS=1 \
UNILUME_FCITX_DIAGNOSTIC_FILE="$PWD/unilume-diagnostic.json" \
fcitx5 -rd
```

Export happens when an input context is destroyed, not in the key-event hot
path. The JSON snapshot is at most 64 KiB, mode `0600`, and is replaced
atomically. One `.previous` snapshot is retained; older data is removed, so
the explicit file export is bounded to two snapshots. Multiple contexts using
one destination intentionally leave the latest two completed snapshots.

Unset both variables and restart Fcitx after collecting the reproduction.
Verbose diagnostics never become persistent configuration and are not enabled
by the application policy or GUI.

## Reading incidents

The bundle distinguishes:

- `capability_loss`: direct replacement became ineligible and the context
  crossed a reset barrier;
- `preedit_handoff`: a complete client preedit was submitted for commit.
  Fcitx does not expose an application acknowledgement for this operation, so
  the count proves the handoff was attempted, not that the application applied
  it;
- `fallback`: the triggering key took the raw no-delete fallback;
- `stale_result`: an old or duplicate completion was rejected;
- `uncertain_outcome`: cancellation could not prove whether a request applied;
- `backend_failure`: even the safe fallback could not be submitted;
- `capability` values `unavailable`, `non_atomic_transport`,
  `invalid_cursor`, `invalid_utf8`, and `resource_limit`: the live
  surrounding snapshot or frontend transaction failed a specific gate.

Durations use six fixed buckets from `under_1_us` through
`at_least_1_ms`; exact timestamps are not exported. The ring retains the last
64 events and cumulative counters for the context.

## Bug reports

Attach the current JSON bundle, its `.previous` file when relevant, the exact
UniLume commit/package version, distribution, Fcitx version, desktop/session,
application name/version and reproduction steps. Inspect the files before
sharing them. Do not attach an Fcitx profile, document, clipboard dump or
screenshots containing private text.

The disabled path performs one predictable branch and does not read the clock,
allocate, lock or write. The Release contract test budgets `beginEvent()` at
no more than 10 ns/key on the test host; broader production performance gates
remain part of Issue #55.
