<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# ADR 0006: Atomic Fcitx replacement is the automatic path

Status: Accepted. Supersedes ADR 0005 for `automatic` mode.

## Decision

UniLume treats non-preedit input as a product invariant. `automatic` uses
verified surrounding text only when the active Fcitx input context implements
`AtomicSurroundingTextInputContext`. It never falls back to client preedit or
uinput. Without the capability, native key events pass through and the status
action reports that atomic replacement is unavailable.

The pinned LumeWorks Fcitx fork adds the optional interface without changing
the existing `InputContext` vtable. D-Bus exposes it only while processing
`ProcessKeyEventBatch`. Wayland v1/v2 issue deletion and committed text before
one protocol commit. Other frontends remain ineligible until their transaction
contract is proven.

`direct` retains acknowledged uinput as an explicit compatibility choice.
`safe-preedit` retains recognizable underlined composition as an explicit
choice. Neither is selected by `automatic`.

## Compatibility

The Fcitx fork is pinned as `vendor/fcitx5`. Standard distro Fcitx remains
loadable, but feature detection fails closed to passthrough. Product packages
must build Fcitx and UniLume from the pinned pair and declare the matching
package revision.

## Consequences

Automatic mode has no preedit update, uinput transaction, direct queue or
cross-transport ordering race. A client without atomic support receives raw
Telex rather than potentially corrupted Vietnamese text.
