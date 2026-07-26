# ADR 0003: Composable typing-convenience pipeline

- Status: accepted
- Date: 2026-07-26
- Issue: #53

## Context

Auto-capitalization, punctuation replacement and optional vowel shortcuts need
small amounts of per-context state. Putting each state machine directly in the
Fcitx event callback would duplicate behavior between direct replacement and
safe preedit, make reset ordering implicit, and couple conveniences to the
inherited UniKey algorithm.

## Decision

`core::TypingPipeline` is the single ordered boundary around
`EngineContext`. It owns no desktop API and does not modify `src/ukengine`.
Both production controllers consume its same `KeyResult`.

The order is fixed:

1. reject/reset on non-text or modified shortcut input;
2. recognize an already-known URL, email, quote or code literal context;
3. finish bounded sequence transforms;
4. apply sentence capitalization;
5. map explicitly configured `w` and bracket shortcuts;
6. call the real per-context UniKey API;
7. update bounded context state from the original key.

All new prose transforms default off. Shortcut scope defaults to `Inherited`,
which is a zero-change fast path preserving the selected UniKey method. The
other closed values are:

- `Disabled`: do not add the shortcut and keep an otherwise standalone key
  literal;
- `NonStart`: apply only after the current token has started;
- `Everywhere`: apply at token start or later.

An existing Telex vowel plus `w` remains a normal UniKey modifier in every
scope. Explicit bracket shortcuts synthesize the selected input method's
documented horn-vowel sequence, so Telex, VNI and VIQR keep their own tone
rules instead of receiving precomposed Unicode behind the engine's back.

Double space applies only after a non-literal word; indentation and repeated
spaces after a literal context are unchanged. Double hyphen is deliberately
completed only by a following space (`word-- ` becomes `word— `). This one-key
lookahead prevents `--flag`, email local parts and code identifiers from being
rewritten. The pipeline never guesses based on application names.

Safe preedit may retain the first candidate space for one key. A
`commit_preedit_before`/`defer_preedit_commit` result contract makes that
holdback explicit; direct replacement ignores those preedit-only flags. The
queue and replacement transaction remain owned by their existing controller.

Return is a sentence boundary distinct from focus/navigation reset. It commits
pending preedit, forwards the key, resets composition, and arms capitalization
only when that option is enabled. Focus, cursor movement, Backspace,
Ctrl/Alt/Super shortcuts, mode changes and configuration reloads clear all
pipeline state.

## Resource and compatibility bounds

The context classifier stores at most 128 raw ASCII bytes. Synthetic output is
bounded and uses the existing engine output limits. There is no file I/O,
locking, heap growth per key, callback chain or retry. When every option is
off/`Inherited`, `process()` takes one branch and delegates directly to
`EngineContext`.

Literal detection is conservative and only suppresses conveniences after
syntax is observable. It is not an autocorrect or language model. A pattern
that cannot be distinguished safely is left unchanged.

## Consequences

- direct and preedit paths cannot diverge in convenience ordering;
- each input context and input method has isolated state;
- configuration changes are composition boundaries;
- future transforms must extend this contract and its interaction matrix, not
  add event-callback state;
- GUI persistence and backup migration remain owned by Issue #50.

