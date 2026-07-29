<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Zero-preedit architecture evidence

This is the evidence record for Issue #47 and
[ADR 0001](adr/0001-composition-ownership.md). It records capability and
failure behavior, not user text.

## Environment

- Date: 2026-07-26
- Debian 13.6, Linux 6.12.95
- Fcitx 5.1.12
- KWin 6.3.6
- Google Chrome 150.0.7871.114
- Firefox ESR 140.13.0
- VS Code 1.129.1, Electron 42.6.0

Firefox came from an unpacked Debian package. Browser profiles, XDG
configuration/data/cache directories, DBus, and the KWin Wayland runtime were
temporary. The main desktop configuration and system packages were not
changed.

Fcitx prints capability flags in hexadecimal without a `0x` prefix.
`SurroundingText` is bit `0x40`; `Preedit` is bit `0x2`.

## Real frontend traces

The X11 trace used the active KDE/X11 session and the autofocus textarea in
`tests/manual-apps/browser-input-probe.html`:

| Frontend | Fcitx frontend | Capability | `SurroundingText` |
| --- | --- | --- | --- |
| Firefox ESR | DBus/GTK IM path | `0x6000000032` | No |
| Google Chrome | DBus/GTK IM path | `0x6000000032` | No |
| VS Code/Electron | DBus/GTK IM path | `0x6000000032` | No |

The native Wayland trace used an isolated virtual KWin compositor started with
its own Fcitx input-method process. Chrome and VS Code used
`--ozone-platform=wayland --enable-wayland-ime`; Firefox used
`MOZ_ENABLE_WAYLAND=1`.

| Frontend | Fcitx frontend | Capability | `SurroundingText` |
| --- | --- | --- | --- |
| Firefox ESR | Wayland | `0x72` | Yes |
| Google Chrome | Wayland | `0x80072` | Yes |
| VS Code/Electron | Wayland | `0x72` | Yes |

The diagnostic group contained one Wayland input context per focused
application and one compositor/Fcitx context. This supports the single
input-method-owner model. It does not justify binding another Wayland
input-method object from the UniLume addon.

Representative redacted trace:

```text
Group [wayland:] has 4 InputContext(s)
program:firefox-esr   frontend:wayland cap:72
program:code          frontend:wayland cap:72
program:google-chrome frontend:wayland cap:80072
program:              frontend:wayland cap:72
```

The trace proves protocol path and capabilities. It does not claim a complete
interactive Wayland application matrix; the deterministic controller tests
cover burst and injected failures, while a later implementation issue must
repeat output validation with the production addon.

## Reproduction outline

X11:

```sh
firefox-esr --no-remote --profile /tmp/unilume-firefox \
  tests/manual-apps/browser-input-probe.html
fcitx5-diagnose
```

Isolated Wayland:

```sh
export XDG_RUNTIME_DIR=/tmp/unilume-runtime
export WAYLAND_DISPLAY=wayland-unilume-47
dbus-run-session -- kwin_wayland \
  --virtual --socket "$WAYLAND_DISPLAY" \
  --no-lockscreen --no-global-shortcuts \
  --inputmethod /usr/bin/fcitx5
```

Applications were then launched in that DBus session with disposable profiles,
followed by `fcitx5-diagnose`. Exact temporary paths are intentionally omitted.

## Executable fault matrix

`integration-zero-preedit-architecture` uses the real
`PreeditFallbackController` and an isolated ownership prototype.

| Candidate | Injected fault/counterexample | Result |
| --- | --- | --- |
| Client preedit | visual update drop/reorder; crash/reconnect | Output remains ordered; pending crash state is discarded, never blindly committed |
| Blind direct replacement | cursor and selection change | Reproduces wrong-range document corruption |
| Server preedit | reordered unacknowledged snapshots | Reproduces stale visible state; the earlier real Firefox 1 ms run also lost text |
| Second Wayland owner | acquire input-method seat already owned by Fcitx | Rejected |
| uinput/helper | focus changes before synthetic delete/insert | Original target stays stale and new target is corrupted |

Issue #102 does not use the blind helper prototype in this table. Its
in-process device exposes only Backspace, emits one event pair at a time, and
waits for each pair to return through Fcitx before a final commit barrier. The
remaining bounded focus window and rollback are recorded in ADR 0001.

The burst matrix runs virtual scheduler intervals of 1, 2, and 5 ms with 1,000
and 10,000 events. Every profile compares the faulted output with the
non-faulted controller output.

The same matrix can be paced against the monotonic wall clock:

```sh
UNILUME_PROTOTYPE_WALL_BURST=1 \
  build/issue47/tests/unilume_integration_tests zero-preedit-architecture
```

The complete wall-paced 88-second matrix exited successfully with no output
mismatch.

A 100-run measurement of the architecture suite processed approximately 8.6
million controller events in 6.004 seconds wall, 5.788 seconds user CPU, and
0.189 seconds system CPU on this machine. This is a deterministic prototype
measurement, not a browser latency claim.

The existing Release integration benchmark supplied the direct-transaction
baseline at 10,000 events per profile:

| Profile | p50 submit latency | Throughput | RSS checkpoints | Correctness |
| --- | ---: | ---: | ---: | --- |
| Immediate | 85 ns | 6.89M keys/s | 3,796 KiB, flat | no lost/duplicate/reordered event |
| Delayed | 76 ns | 6.04M keys/s | 3,836 KiB, flat | queue drained; no lost/duplicate/reordered event |
| Stale observation | 146 ns | 5.69M keys/s | 3,836 KiB, flat | 1,024 unsafe edits aborted; no lost/duplicate/reordered event |

These are in-process controller numbers, not compositor or application
round-trip latency. They are useful for detecting regression, not for claiming
that a protocol path is faster than a browser.

The wall-clock soak command is:

```sh
UNILUME_PROTOTYPE_SOAK_SECONDS=1800 \
  build/issue47/tests/unilume_integration_tests zero-preedit-soak
```

It continuously checks exact Vietnamese output and samples RSS in one process.
The final duration, iteration count, and RSS values are printed for the issue
record.

The accepted run completed 1,800 seconds and 68,235,392 iterations with no
output error. Minute checkpoints stayed between 3,728 and 3,732 KiB and did
not show sustained growth.

## Primary protocol references

- [Fcitx: Using Fcitx 5 on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland/en)
- [Wayland input-method v2](https://wayland.app/protocols/input-method-unstable-v2)
- [Wayland text-input v3](https://wayland.app/protocols/text-input-unstable-v3)
- [Linux uinput documentation](https://kernel.org/doc/html/latest/input/uinput.html)
