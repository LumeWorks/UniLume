# Verified direct replacement backend

Issues #48 and #102 implement the two transports of the single Fcitx
replacement backend accepted by [ADR 0001](adr/0001-composition-ownership.md).
Both remain in-process parts of the input-method engine.

## Eligibility

`VerifiedDirectEnabled` is an adapter-level feature flag and defaults to
`True`. Enabling it only makes a context eligible. An atomic direct operation
still requires:

- Fcitx `SurroundingText` capability;
- a frontend transport that can apply delete plus commit as one indivisible
  edit;
- a valid surrounding snapshot no larger than 64 KiB;
- valid UTF-8;
- a collapsed cursor/anchor selection;
- enough characters before the cursor for the requested deletion.

The backend checks this live snapshot inside the replacement request. It does
not infer validity from application identity or text previously committed by
UniLume. Automatic and explicit-direct application modes both retain this
gate; failure selects or returns to safe preedit.

Fcitx's `dbus`, `wayland` and `wayland_v2` frontends do not satisfy the atomic
transport gate. On Linux, UniLume instead uses one shared Backspace-only
uinput device when `/dev/uinput` is available. It emits one deletion at a
time, waits for its press/release to return through the same Fcitx input
context, and commits only after a final filtered barrier. Deletions are capped
at 128 characters. If the device is unavailable, these frontends use safe
preedit.

## Transaction contract

Verified replacement and raw fallback are separate backend operations:

- verified replacement may delete and commit only after the live checks;
- raw fallback can commit the triggering UTF-8 key but can never delete.

A synchronous `failed` result means no part of a request was applied. A
pending request may be replayed as raw text only after `cancel()` confirms it
was not applied. An uncertain cancellation is counted explicitly, fenced by a
backend reset, and never speculatively replayed. This chooses a visible missed
key in an impossible-to-prove outcome over duplicated or deleted document
text; the next event starts from a reset engine generation.

If a synchronous raw fallback itself fails, the controller returns that key
event to Fcitx instead of filtering it and records a fallback-failure metric.

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

Set `VerifiedDirectEnabled=False` to keep all contexts on safe preedit without
changing application policy files. A package rollback uses the user-local
procedure in [fcitx5-addon.md](fcitx5-addon.md): select the previous input
method, restore the Fcitx group configuration, restart Fcitx, and remove only
UniLume's module and metadata files.

Qualification and any later decision to change the default belong to the
cross-application and Wayland acceptance issues. A direct-mode rule or hotkey
cannot bypass the feature flag or live-state checks.
