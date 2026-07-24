<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Stable-prefix commit (decision)

**Status:** research complete — **decision C (commit-only model)**  
**Issue:** #24  
**Not production-ready.** No hybrid stable-prefix path is enabled in the
Fcitx5 addon or core controllers.

## Goal

Reduce client-preedit underline length in browser/Electron contexts that lack
`SurroundingText`, **without** losing, duplicating, or reordering text, and
without breaking backspace or tone relocation.

Zero-preedit is **not** required.

## Decision

**C — not safely achievable as a commit-only stable-prefix** with the current
engine architecture. A forwarded-backspace architecture (forward Backspace to
the application when backspace enters committed text) has not been studied and
is not covered by this decision.

Evidence and counterexamples:
[composition-span-research.md](composition-span-research.md).

Automated locks:
`tests/engine/composition_span_counterexample_tests.cpp`.

## What remains the safe design

| SurroundingText | Path |
| --- | --- |
| Available and trustworthy | Direct zero-preedit (`DirectCommitController`) |
| Unavailable / untrusted | Full client preedit for the current word (`PreeditFallbackController`), commit on word/composition boundary |

Word-boundary commit already shortens underline relative to multi-word
preedit. Mid-word commit would require a proven mutable-span oracle the
legacy engine does not expose.

## Oracle contract (fail-closed for commit-only)

Until an engine-owned proof exists:

```text
stable_prefix_bytes = 0
mutable_suffix_bytes = full composition length
```

Guessing via longest common prefix of successive outputs is **forbidden**.

## Feature flag

No `UNILUME_ENABLE_STABLE_PREFIX` production path is introduced. Enabling a
hybrid controller without a proven oracle would be incorrect by default.

## Real applications

No hybrid stable-prefix real-app matrix is claimed. Browser/Electron remain on
full client preedit in the tested X11 matrix
([browser-input-policy.md](browser-input-policy.md),
[real-application-validation.md](real-application-validation.md)).

Wayland: not used as evidence for this decision beyond existing
[wayland-validation.md](wayland-validation.md) notes.

## Revisit criteria

Reopen A/B for a commit-only model only if:

1. UniKey (or a minimal in-engine helper) exposes a proven mutable-start index
   after each key;
2. the index is monotonic for non-backspace continuations within a composition;
3. backspace/restore contracts are explicit;
4. continuation corpus + sanitizers show zero invariant failures;
5. hybrid remains behind a default-OFF flag until real-app validation passes.

A forwarded-backspace architecture would need a separate evaluation.
