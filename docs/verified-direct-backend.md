# Verified direct replacement backend

Issues #48 and #102 implement the two transports of the single Fcitx
replacement backend accepted by [ADR 0001](adr/0001-composition-ownership.md).
Both remain in-process parts of the input-method engine.

## Eligibility

`VerifiedDirectEnabled` is an adapter-level feature flag and defaults to
`True`. Enabling it only makes a context eligible. An atomic direct operation
still requires:

Fcitx `dbus`, `wayland`, and `wayland_v2` are split transports. With the
Backspace-only uinput device available, the backend dispatches the required
deletion Backspaces sequentially after the physical triggering key is released.
The final deletion release is the commit boundary. The transaction tracks press/release
events to consume duplicate events or cancelled remnants.
Shortcuts arriving before dispatch cancel safely; shortcuts arriving after dispatch
pass through immediately, fence context, and leave remnants to be consumed.

`DirectStrategy=Fast` is the default and commits text at the final deletion release.
`Guarded` is an opt-in strategy that also verifies
a refreshed surrounding snapshot when the frontend has published it; neither
strategy is a claim of transport atomicity.

Fcitx's `dbus`, `wayland` and `wayland_v2` frontends do not satisfy the atomic
transport gate. On Linux, UniLume instead uses one shared Backspace-only
uinput device when `/dev/uinput` is available. It emits one deletion at a
time, waits for its press/release to return through the same Fcitx input
context, and commits only after a final filtered barrier. Deletions are capped
at 128 characters. If the device is unavailable, these frontends use safe
preedit.

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

The queue holds at most 512 inputs, each with at most 32 bytes. Focus,
navigation, configuration, policy, capability, and frontend lifecycle
boundaries cancel or fence the active transaction, clear the queue, reset the
engine, and advance the backend generation. A new Fcitx input context starts
with a fresh state object.

Fcitx delete/commit calls are synchronous requests on its event thread, but
synchronous calls are not necessarily one frontend transaction. The
production backend does not block the event loop, sleep, or maintain an
asynchronous worker. Split transports use the acknowledged path above rather
than issuing a non-atomic surrounding-text edit.
The delayed simulator exists only to exercise cancellation, stale, duplicate,
reordered, dropped, and uncertain outcomes deterministically.

## Rollback

Set `VerifiedDirectEnabled=False` to disable every direct backend without
changing policy files. Package rollback follows the user-local procedure in
[fcitx5-addon.md](fcitx5-addon.md): select the previous input method, restore
the Fcitx configuration, restart Fcitx, and remove only UniLume-owned module
and metadata files.
