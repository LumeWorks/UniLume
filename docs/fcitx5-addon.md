<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Experimental Fcitx5 addon

UniLume now has an optional Fcitx5 input-method addon. It is an experimental
MVP, not a production-ready package.

## Build

Install CMake, gettext, pkg-config, a C++23 compiler, and Fcitx5 core
development files.
On Debian-family systems the Fcitx dependency is:

```sh
sudo apt-get install gettext libfcitx5core-dev libfcitx5config-dev pkg-config
```

Build and install into a disposable prefix:

```sh
PREFIX="$(mktemp -d)"

cmake -S . -B build/fcitx5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNILUME_BUILD_FCITX5_ADDON=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build/fcitx5 --parallel 2
cmake --install build/fcitx5
```

The prefix contains:

```text
lib/fcitx5/unilume.so
share/fcitx5/addon/unilume.conf
share/fcitx5/inputmethod/unilume.conf
share/applications/org.fcitx.Fcitx5.Addon.UniLume.desktop
share/icons/hicolor/scalable/apps/unilume.svg
share/icons/hicolor/scalable/apps/unilume-off.svg
share/icons/hicolor/scalable/apps/unilume-fallback.svg
share/locale/vi/LC_MESSAGES/unilume.mo
share/metainfo/org.fcitx.Fcitx5.Addon.UniLume.metainfo.xml
```

With the option left `OFF`, normal core builds do not require Fcitx5. With the
option `ON`, configure fails clearly if the development package is missing.

For a user installation, choose a prefix recognized by that Fcitx5 package,
reload/restart Fcitx5, and add UniLume through `fcitx5-configtool`. Distribution
paths differ, so packaging must set the final prefix explicitly.

## Direct-commit behavior

Each Fcitx input context owns an independent engine/controller state. Ordinary
text is committed immediately. When Telex changes text already committed,
the backend requests the minimum surrounding-text deletion and commits the
replacement in the same synchronous controller transaction. Cursor movement,
focus changes, reset events, and unhandled Backspace clear composition state.

When `VerifiedDirectEnabled=True`, an input context with a valid live
surrounding-text snapshot on its first key uses direct commit with no client
preedit and therefore no underline. The flag defaults to `False` until the
production matrices are complete. A context without the flag or live oracle
uses the safe preedit fallback. Frontends may display that client preedit with
an underline. The addon never promotes a live preedit context into direct mode,
because a frontend may still be applying an asynchronous preedit update.

## Safety fallback

Replacement requires the Fcitx `SurroundingText` capability and a bounded,
valid UTF-8 snapshot with an unselected cursor and enough characters. Previously
committed text never bypasses this live check. Verified replacement and raw
fallback use separate backend calls so fallback cannot guess a deletion.

This policy prefers a visible uncomposed key over duplicate or missing text.
It does not synthesize Backspace, sleep, retry indefinitely, use a socket
daemon, or provide a uinput fallback.

Fcitx delete/commit methods are synchronous requests on its event thread and
do not acknowledge application-side mutation. See
[real-application-validation.md](real-application-validation.md) for the
tested frontend matrix and the reason Firefox remains on fallback.
The complete lifecycle, feature flag, failure semantics, and rollback contract
are documented in
[verified-direct-backend.md](verified-direct-backend.md).

## Optional typing conveniences

Auto-capitalization, word-ending double-space replacement, prose `-- `
replacement and scoped `w`/bracket shortcuts run through the same per-context
pipeline in both direct and preedit paths. Prose transforms default off;
shortcut scope defaults to `Inherited`. Modified shortcuts, URL/email/code
literal contexts and reset boundaries are never rewritten. See
[ADR 0003](adr/0003-typing-convenience-pipeline.md) for exact ordering and
scope semantics.

## Status actions and localization

The Fcitx status menu exposes the effective application mode, Telex/VNI/VIQR,
lossless UTF-8 output, spell checking, macros and personal dictionary state.
Input-method and option changes are partial updates to the same validated
Fcitx configuration contract; the new state crosses the existing composition
boundary before another key is processed. Macros and dictionaries cannot be
enabled from the status menu until their validated file is configured.

The input-method icon distinguishes normal Vietnamese processing, off, and
safe-preedit fallback. All three are scalable SVGs installed in the hicolor
theme for KDE/GNOME and HiDPI fallback. English source strings and the complete
Vietnamese gettext catalog cover status tooltips and configuration
descriptions. Run `scripts/i18n/update.sh` after changing a production string;
CI rejects stale or invalid catalogs and validates the desktop/AppStream
metadata.

## Diagnostics

Diagnostics remain disabled by default. An explicit environment opt-in enables
a fixed per-context ring of path, capability, queue, reset, outcome and coarse
duration signals. No typed/preedit/surrounding text is accepted by the trace.
Optional JSON export is atomic, private and bounded to current plus previous
snapshots. Setup and bug-report guidance are in
[diagnostics.md](diagnostics.md).

## Verification status

Automated:

- addon compilation and dynamic linking with Fcitx5 5.1.12 headers/runtime;
- install into a temporary prefix and metadata/factory-symbol checks;
- controller tests for immediate, delayed, stale, duplicate, reordered,
  dropped, failure, reset, burst, and sanitizer profiles.

Controlled desktop validation has covered xterm, KWrite, Zenity, VSCode,
Chrome, and Firefox ESR on KDE/X11. Direct zero-preedit was observed in the
tested Qt and GTK contexts; XIM and browser/Electron contexts used fallback.
This is still a limited environment matrix, not a production-readiness claim.

In the tested Debian 13.6 / KDE Plasma / X11 matrix, browser and Electron
contexts used the client-preedit fallback because they did not advertise the
`SurroundingText` capability to Fcitx. See `docs/browser-input-policy.md` for
the capability analysis and input-path policy state machine. Wayland or other
environments may produce different capability signals and are not yet
verified.

Wayland has not been tested. See `docs/wayland-validation.md` for the manual
validation checklist and environment-check script.

The addon exposes Telex, VNI, and VIQR plus the four verified behavior options
documented in [unikey-options.md](unikey-options.md). UTF-8 remains the only
output charset. Production macros use `MacroEnabled` and `MacroFile`; tables
are validated outside the key path and installed per context as documented in
[macro-support.md](macro-support.md). Personal dictionaries use
`DictionaryEnabled` and `DictionaryFile` with the same atomic per-context
lifecycle described in [dictionary-support.md](dictionary-support.md).
Per-application modes, status menu, configurable hotkeys, deterministic rule
precedence, and safe direct-mode fallback are documented in
[application-policy.md](application-policy.md). Legacy
charset output, uinput, distro packages, and system-wide Wayland protocol
integration are not provided yet.
