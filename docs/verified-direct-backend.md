# Verified direct replacement backend

Issue #48 hardens the single Fcitx replacement backend accepted by
[ADR 0001](adr/0001-composition-ownership.md). It remains an in-process part
of the Fcitx input-method engine. No second text writer or desktop backend is
introduced.

## Eligibility

`VerifiedDirectEnabled` is an adapter-level feature flag and defaults to
`False` until the production application matrices are complete. Enabling it
only makes a context eligible. Every direct operation still requires:

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

Fcitx's `dbus`, `wayland` and `wayland_v2` frontends do not satisfy the
transport gate. Their public addon API dispatches deletion and committed text
separately. The Wayland v2 frontend flushes each with its own protocol
`commit(serial)`; 30-minute native GTK3 soaks also retain partially applied
edits through the D-Bus frontend. UniLume therefore uses client preedit for
these frontend protocols even when they advertise `SurroundingText`. This is
a protocol contract, not an application-name rule. Direct replacement can be
reconsidered only when Fcitx exposes an atomic replacement primitive to
input-method addons.

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

The queue holds at most 64 inputs, each with at most 32 bytes. Focus,
navigation, configuration, policy, capability, and frontend lifecycle
boundaries cancel or fence the active transaction, clear the queue, reset the
engine, and advance the backend generation. A new Fcitx input context starts
with a fresh state object.

Fcitx delete/commit calls are synchronous requests on its event thread, but
synchronous calls are not necessarily one frontend transaction. The
production backend does not block the event loop or maintain an asynchronous
worker, and it refuses a known split transport before issuing either request.
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
