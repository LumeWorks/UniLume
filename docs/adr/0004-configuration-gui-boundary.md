<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# ADR 0004: Qt configuration GUI behind the Fcitx config boundary

## Status

Accepted.

## Context

UniLume needs one production editor for all addon options and managed
resources. Reading engine memory, maintaining a second schema or inventing a
private IPC service would split validation and lifecycle ownership.

## Decision

Build an optional Qt6 Widgets application using Qt DBus and the official
Fcitx5Qt6DBusAddons controller proxy. The GUI exchanges complete snapshots
through Fcitx `GetConfig` and `SetConfig`. Its toolkit-independent model,
backup codec and generation transaction reuse the production option and
resource contracts.

Qt and Fcitx Qt libraries are distribution dependencies and are never bundled.
The core and addon remain buildable without the GUI. Desktop palette, font,
scale and icon theme own appearance.

## Consequences

- Fcitx remains the only runtime configuration authority.
- GUI failure cannot terminate or corrupt the input-method process.
- Resource edits can be validated and staged before one snapshot switch.
- The normal per-key path gains no Qt, D-Bus, backup or file operation.
- Distributions enabling the GUI must package Qt6 Widgets, Qt DBus and
  Fcitx5Qt6DBusAddons.
