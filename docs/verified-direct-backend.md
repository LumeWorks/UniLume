# Verified direct replacement backend

Issues #48, #102, and #107 define the two transports of the single in-process
Fcitx replacement backend. The active ownership decision is
[ADR 0005](adr/0005-direct-only-fcitx-runtime.md).

## Eligibility

`VerifiedDirectEnabled` defaults to `True`. When it is false, Direct becomes
raw passthrough. An atomic direct operation requires Fcitx surrounding text, a
transport that applies delete plus commit indivisibly, a valid UTF-8 snapshot
no larger than 64 KiB, a collapsed cursor/anchor, and enough text before the
cursor. Application identity never bypasses these checks.

Fcitx `dbus`, `wayland`, and `wayland_v2` are split transports. With the
Backspace-only uinput device available, the backend dispatches the required
deletion Backspaces plus one sentinel after the physical triggering key is
released in one bounded kernel write. The transaction tracks dispatched press/release
events to consume the sentinel, duplicate events, or cancelled remnants.
Shortcuts arriving before dispatch cancel safely; shortcuts arriving after dispatch
pass through immediately, fence context, and leave remnants to be consumed.

`DirectStrategy=Fast` is the default and commits text at sentinel press.
`Guarded` is an opt-in strategy that commits at sentinel release while verifying
a refreshed surrounding snapshot when the frontend has published it; neither
strategy is a claim of transport atomicity.

## Failure contract

A pre-dispatch failure guarantees no direct mutation and returns the original
key event to Fcitx. The controller never falls back to preedit or a synthetic
raw commit.

Once a synthetic pair has been dispatched, partial write, cancellation, focus or
generation loss, stale ordering, and failed Guarded validation are uncertain.
They commit no replacement text, increment the uncertainty diagnostics, fence
the backend generation, and leave the context in passthrough until reset. A
duplicate or stale completion cannot commit twice.

The controller queue is fixed at 512 inputs of at most 32 bytes each. The
uinput path operates without a timer, sleep, retry loop, helper daemon, mouse listener,
or second thread. Atomic Fcitx calls remain synchronous on its event thread.

## Shortcut and literal passthrough boundaries

Ctrl, Alt, Super, Meta, Hyper, AltGr, Tab/Shift+Tab, Shift+Enter, modified Backspace,
navigation, Delete, Escape, and other non-text keys are classified before ACK and engine.
They fence local state and pass through without filtering, committing, or enqueueing.
Unchanged text (delete_before_cursor == 0 and commit_text == input.text) maintains engine state
while passing the key event through directly. Direct replacement backends are called only
when Telex or convenience transforms actually modify text. Plain Enter resets line state and
passes through; plain Backspace enters engine/ACK processing. Shift with printable text passes to
the engine for capitalization.

Focus, deactivation, configuration, policy, and capability boundaries clear
the controller queue and advance the backend generation. A partial dispatched
transaction is never replayed into a new target.

## Rollback

Set `VerifiedDirectEnabled=False` to disable every direct backend without
changing policy files. Package rollback follows the user-local procedure in
[fcitx5-addon.md](fcitx5-addon.md): select the previous input method, restore
the Fcitx configuration, restart Fcitx, and remove only UniLume-owned module
and metadata files.
