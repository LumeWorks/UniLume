<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Configuration GUI

`unilume-config` is the optional Qt6 configuration application for the
Fcitx5 addon. It is built only when both `UNILUME_BUILD_FCITX5_ADDON=ON` and
`UNILUME_BUILD_CONFIG_GUI=ON`.

## Boundary

The application uses the official Fcitx controller
`GetConfig`/`SetConfig` D-Bus API at
`fcitx://config/inputmethod/unilume`. It neither reads engine memory nor
introduces a daemon. The toolkit-independent model uses the same production
configuration and resource decoders as the addon; key-event handling never
calls GUI, D-Bus, backup or file code.

Qt6 Widgets, Qt DBus and Fcitx5Qt6DBusAddons are system dependencies. They are
not bundled. This choice and its consequences are recorded in
[ADR 0004](adr/0004-configuration-gui-boundary.md).

## Option coverage

The General, Typing and Shortcuts pages generate controls from typed
descriptors. Applications, Macros, Dictionary and Keymap use canonical text
editors so the production parsers remain the only grammar implementation.
Appearance deliberately follows the desktop palette, font scale, icon theme
and device-pixel ratio; private visual settings would duplicate stale desktop
state. Backup owns no runtime option.

Every field in `InputMethodConfig` has exactly one descriptor. Managed file
path fields are represented by their editable resource document rather than a
machine-specific path.

## Validation and apply transaction

Validation checks typed choices and booleans, normalized Fcitx hotkeys,
cross-action hotkey conflicts and all four resource grammars. An error names
the exact option; resource errors also include parser line/field data where
available. Invalid settings cannot reach `SetConfig`.

Apply follows this transaction:

1. validate the complete staged model;
2. write each resource into a new private, synced generation;
3. submit one complete Fcitx configuration snapshot;
4. reload and compare the accepted snapshot;
5. keep the active and one previous generation for rollback.

If submission or verification fails, the previous snapshot is resubmitted and
the failed generation is removed. Cancel and import preview only change widget
state. A GUI crash therefore cannot stop Fcitx or expose a partial resource
generation.

## Backup format

The `.ulbackup` format is a strict, length-framed UTF-8 document with
`unilume_backup_version=1`, every production field and the four canonical
resource documents. Unknown, duplicate, oversized, truncated or future data
is rejected. Managed paths are exported empty and recreated on Apply, making
the backup portable. Version 0 is migrated through current defaults; imported
values are previewed before they become staged state.

## Accessibility and verification

All controls have labels or accessible names, tabs and actions are reachable
by keyboard, and validation focus moves to the failing field. The GUI uses
layout-managed sizes and scalable theme icons for HiDPI and light/dark
palettes.

Automated checks cover descriptor completeness, production parser validation,
backup round-trip/migration/corruption, generation rollback guards, dirty
state, tab/control accessibility and an isolated live Fcitx D-Bus round-trip.
Manual desktop acceptance should exercise both a KDE session and a
GTK-oriented desktop at 100% and HiDPI scale without changing the operator's
normal Fcitx profile.
