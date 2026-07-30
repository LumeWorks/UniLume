# Architecture

UniLume currently preserves the x-unikey module boundaries instead of
rewriting the input algorithm.

```text
platform adapter (future)
        |
        v
src/ukinterface   C-compatible key-event API
        |
        v
src/ukengine      UniKey Vietnamese input state machine
        |
        +----> src/vnconv   legacy charset and UTF-8 conversion
                     |
                     +----> src/byteio
```

`tests/engine/` exercises the real `ukinterface` API and simulates the small
responsibility a platform adapter has: echo unchanged keys, apply requested
backspaces, and append replacement bytes.

The historical integrations under `src/platform/legacy/` are retained for
provenance and reference but are not built by CMake. They depend on old
X11/GTK2 interfaces and should not be presented as supported UniLume frontends.
`third_party/imdkit/` is bundled third-party X11R6 code with separate notices.

The build now has three explicit layers: the inherited engine, an opaque
per-context C facade in `src/ukinterface/`, and a C++23 direct-commit layer in
`src/core/`. `DirectCommitController` assigns monotonic sequence IDs and
applies delete-plus-commit as one bounded transaction. It rejects stale or
duplicate completions and resets safely when the backend cannot establish a
trustworthy replacement.

`src/platform/simulation/` provides a deterministic backend for delayed,
stale, reordered, dropped, and failed operations without a desktop session.
The optional `src/fcitx5/` addon maps Fcitx events into the same controller and
keeps one state object per Fcitx input context. It is an experimental Telex
MVP, not a supported production frontend.

Each Fcitx context resolves an explicit application policy with only Direct
and Off modes. Direct uses an eligible atomic or acknowledged split-transport
backend; when none is available it passes the original event through. Off is
unconditional passthrough. The production Fcitx state has no preedit owner.
Policy and mode changes cross a composition reset barrier. The schema and
precedence are documented in
[application-policy.md](application-policy.md).

The production direct controller never invokes its retained no-delete fallback
operation. It validates the live bounded snapshot inside atomic edits and
fences old frontend generations on reset. It is feature-gated during
qualification; see
[verified-direct-backend.md](verified-direct-backend.md).

The current production ownership decision is recorded in
[ADR 0005](adr/0005-direct-only-fcitx-runtime.md): `InputContextState` exposes
Direct/Off only and never connects the retained core preedit controller to
Fcitx. An in-process Backspace-only uinput device supplies a sentinel ordering
boundary on split transports.

The original proposal and boundary rationale remain in
[linux-adapter-design.md](linux-adapter-design.md). Current test semantics and
the addon limitations are documented in
[integration-testing.md](integration-testing.md) and
[fcitx5-addon.md](fcitx5-addon.md).

Production behavior options cross the adapter boundary only through the typed
mapping documented in [unikey-options.md](unikey-options.md). Each option
snapshot belongs to one engine context; the legacy global option API is not
used by the Fcitx path.

Personal dictionary policy is a boundary-stage consumer of the real engine
result, not a replacement spelling algorithm. Its immutable snapshot,
precedence and resource bounds are frozen in
[ADR 0002](adr/0002-personal-dictionary-policy.md).

Optional typing conveniences share one ordered per-context boundary around the
real engine. Their stage order, reset semantics, safe-preedit holdback and
literal-context policy are frozen in
[ADR 0003](adr/0003-typing-convenience-pipeline.md). No convenience state
lives in the Fcitx callback or inherited UniKey algorithm.

Production diagnostics are input-context-local and event-loop confined. The
disabled path does not read a clock; the opt-in path stores only closed numeric
or enum fields in a fixed ring and performs export after the context stops.
The privacy inventory, incident vocabulary and disk bounds are documented in
[diagnostics.md](diagnostics.md).

Mid-composition stable-prefix commit (commit immutable UTF-8 bytes while
keeping a mutable client-preedit suffix, without SurroundingText) was researched
under Issue #24. The legacy engine does not expose a proven monotonic mutable
span; decision **C** (commit-only model) and counterexamples are recorded in
[composition-span-research.md](composition-span-research.md) and
[stable-prefix.md](stable-prefix.md).
