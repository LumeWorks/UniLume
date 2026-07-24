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

Each Fcitx context selects one application path on its first processable key.
Reliable surrounding-text contexts use direct replacement; contexts without
that capability retain a client-preedit fallback. A direct context may demote
after capability loss, but a preedit context is never promoted in place. This
prevents asynchronous frontend updates from overlapping the first direct
replacement.

The original proposal and boundary rationale remain in
[linux-adapter-design.md](linux-adapter-design.md). Current test semantics and
the addon limitations are documented in
[integration-testing.md](integration-testing.md) and
[fcitx5-addon.md](fcitx5-addon.md).

Mid-composition stable-prefix commit (commit immutable UTF-8 bytes while
keeping a mutable client-preedit suffix, without SurroundingText) was researched
under Issue #24. The legacy engine does not expose a proven monotonic mutable
span; decision **C** (commit-only model) and counterexamples are recorded in
[composition-span-research.md](composition-span-research.md) and
[stable-prefix.md](stable-prefix.md).
