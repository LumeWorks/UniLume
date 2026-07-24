<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Composition span research (UniKey engine)

**Issue:** #24  
**Decision:** **C** — a commit-only, no-forwarded-deletion stable-prefix is not
safely achievable with the current engine architecture, without a deep rewrite
that would duplicate or restructure UniKey rules. A forwarded-backspace
architecture (forward Backspace to the application when backspace enters
committed text) has not been studied and is not covered by this decision.  
**Date:** 2026-07-24

This document answers whether UniLume can expose a *proven* mutable composition
span and use it to commit a *stable prefix* into browser/Electron clients that
lack `SurroundingText`. Priorities remain: no lost / duplicate / reordered
characters and correct backspace / tone relocation **before** any underline
reduction.

## 1. Related engine state

```text
                    UkEngine (per UlEngineContext)
┌─────────────────────────────────────────────────────────────┐
│ m_buffer[MAX_UK_ENGINE] : WordInfo[]   composed symbols     │
│   form, c1Offset, vOffset, c2Offset                         │
│   caps, tone, vnSym, keyCode                                │
│ m_current               : last symbol index                 │
│ m_changePos / m_backs   : rewrite window for next output    │
│                                                             │
│ m_keyStrokes[] / m_keyCurrent : raw key events (replay)     │
│                                                             │
│ UkSharedMem options: freeMarking=1 (default),               │
│   autoNonVnRestore=1, spellCheckEnabled=1, ...              │
└─────────────────────────────────────────────────────────────┘
         │ process() / processBackspace() / restoreKeyStrokes()
         ▼
   writeOutput(m_changePos .. m_current) → UTF-8 delta + backs
```

| Question | Finding |
| --- | --- |
| Raw keystrokes or composed symbols? | **Both.** `m_buffer[]` holds composed `WordInfo` (vnSym + tone + caps). `m_keyStrokes[]` keeps raw events for restore/replay. |
| Word / composition buffer? | Yes. `m_buffer` is a rolling composition buffer (max 128). Word forms: `vnw_c`, `vnw_v`, `vnw_cv`, `vnw_vc`, `vnw_cvc`, `vnw_nonVn`, `vnw_empty` (break). |
| Where is the tone stored? | On a **specific** `WordInfo.tone` at the tone position of the current vowel sequence (`getTonePosition`), not necessarily on the last key. |
| What can still change? | Any symbol from the earliest `markChange(pos)` target through `m_current`: roof/hook/bowl on vowels, `đ`, tone place/remove/relocate, freeMarking rewrites, non-VN restore of the whole last word. |
| Does backspace replay the word? | Not a full Telex replay of the raw sequence in the common path. It pops `m_current` and **may relocate tone** to a new position, rewriting earlier symbols (`processBackspace` + `writeOutput`). Keystroke buffer is synchronized via `synchKeyStrokeBuffer`. |
| Syllable / word span concept? | Implicit via `form` and offsets (`c1Offset`, `vOffset`, `c2Offset`). No public “mutable start index” API. |
| Mutable start from current state? | Only by re-deriving UniKey rules (which symbols `processTone` / `processRoof` / `processHook` / `processDd` may still target). That is a second engine. |
| Index unit | Internal: **symbol slots** in `m_buffer`. Output: **UTF-8 bytes** (variable width). Backspaces for UTF-8 charset count **characters** (`getSeqSteps`), not bytes. |
| Can a later key change text before a guessed mutable start? | **Yes** — see counterexamples. |

Default context options (`unilume_context.cpp`): `freeMarking = 1`,
`autoNonVnRestore = 1`, `spellCheckEnabled = 1`. Free marking deliberately
allows marks on non-final positions.

## 2. What “stable prefix” would require

A stable prefix of the current composed UTF-8 string is safe to
`commitString` into a client **without** SurroundingText only if:

1. **Forward immutability:** every legal continuation key changes only the
   suffix after that prefix (tone, roof, hook, `đ`, append, punctuation).
2. **Monotonicity within a composition:** once published as stable, the prefix
   length never decreases (committed text cannot be recalled).
3. **Backspace safety:** either backspace never needs to delete into the
   committed prefix, or the controller falls back *before* that can happen —
   without deleting committed bytes blindly.
4. **Restore safety:** `restoreKeyStrokes` / `autoNonVnRestore` must not rewrite
   committed bytes.
5. **UTF-8:** the split is on a code-point boundary.

Longest common prefix (LCP) between output at step *t* and *t+1* is only an
**observation**. It is not a proof that the shared bytes are immutable under
*other* future keys.

## 3. Candidate oracles (rejected or limited)

### 3.1 LCP of successive engine outputs

**Rejected.** LCP often grows while earlier symbols remain mutable
(tone/roof still applicable). Continuations can rewrite the “stable” region.

### 3.2 Everything before `m_changePos` after a key

**Rejected.** `m_changePos` is the start of the *last rewrite window*, not the
immutable frontier for *future* keys. The next key may `markChange` further
left.

### 3.3 Everything before current vowel sequence (`vStart`)

**Rejected as a general oracle.**

- Initial consonants remain mutable (`d` → `đ` via second `d`).
- Multi-vowel sequences relocate tone across the whole vowel run.
- Free marking and roof/hook can rewrite earlier vowels.
- Even a “complete” looking syllable can change when a suffix consonant is
  added (tone position depends on `terminated` in `getTonePosition`).

### 3.4 Entire last word is always mutable; only prior words are stable

**Partially true inside the engine buffer**, but:

- Client preedit (see `PreeditFallbackController`) already commits on
  whitespace / boundary and keeps only the **current word** as preedit.
- Mid-word underline reduction is exactly what browsers need; this oracle
  does not reduce that underline.
- `restoreKeyStrokes` rewrites the whole last word at word-end, which is
  already handled by keeping the word in preedit until boundary.

### 3.5 Full symbol-level reachability analysis

In principle, for each buffer state, compute the set of indices that any
single next event in the engine’s event space could `markChange`. Stable
prefix = symbols strictly left of that set.

**Not adopted:**

- Requires encoding Telex/VNI/VIQR + freeMarking + tone tables outside the
  legacy engine (forbidden “second engine”).
- Incomplete event space ⇒ false stable prefixes.
- Backspace and restore still break monotonic commit without SurroundingText.

## 4. Counterexamples (proof sketches)

These are automated in `tests/engine/composition_span_counterexample_tests.cpp`.

| ID | Sequence (Telex) | Why a non-zero mid-word prefix is unsafe for a commit-only model |
| --- | --- | --- |
| CE1 | `d` then `d` | Second `d` rewrites first character to `đ` (UTF-8 length changes). Any commit of `d` is wrong. |
| CE2 | `to` then `o` | `to` → `tô`; roof applies to earlier vowel. |
| CE3 | `hoa` then `s` | Tone applied to an earlier vowel (`hoa` → `hóa`); earlier-symbol tone rewrite. |
| CE4 | `aa` then tone key | Roof + tone rewrite earlier `a`. |
| CE5 | any partial word + Backspace | May relocate tone and rewrite earlier symbols; a commit-only controller cannot recall committed bytes without SurroundingText. |
| CE6 | non-VN sequence + space with `autoNonVnRestore` | Whole last word may be replaced by raw keystrokes at boundary. |
| CE7 | LCP after `t`→`to` is `"t"`, then `ooi` path | Continuations rewrite shared LCP bytes; LCP ≠ immutability. |

## 5. Interaction with existing controllers

| Path | Behavior today | Stable-prefix impact |
| --- | --- | --- |
| Direct + SurroundingText | Delete+replace via backend | Can rewrite; does not need stable prefix. |
| Preedit fallback | Full current word as client preedit; commit on boundary | Already the **safe** no-SurroundingText design. |
| Hybrid stable-prefix (commit-only) | Commit mid-word; keep suffix preedit | Requires oracle; without it, lost/orphan text on backspace or tone rewrite. A forwarded-backspace variant has not been studied. |

Without SurroundingText, **any** mid-word commit in a commit-only model is
irreversible. The only correct reaction to uncertainty in that model is
`stable_prefix = 0` (full preedit). A forwarded-backspace architecture has
not been evaluated.

## 6. Backspace and monotonicity

`processBackspace` can:

- shrink the composition by one symbol, and/or
- move tone to a new index and emit a multi-character rewrite.

If a controller had already committed a non-empty prefix:

- **Commit-only model:** deleting into that prefix needs SurroundingText
  (unavailable on target browsers), or the committed result becomes orphaned
  text that violates "no lost / no garbage characters".
- **Forwarded-backspace model:** the controller could forward a Backspace key
  to the application and rebuild engine state. This approach has not been
  studied in this research — it raises orthogonal concerns (selection, cursor
  position, application-IM ordering, async browser behavior) that are outside
  the scope of a pure engine mutability analysis.

Therefore a **commit-only** oracle would also need a **backspace contract**:
either forbid commit until word end, or guarantee backspace never crosses the
committed frontier. The engine does not provide such a frontier.

## 7. Indexing and UTF-8

Even a perfect symbol-index oracle would need a careful map:

```text
symbol index i  →  UTF-8 byte offset of writeOutput(0..i-1)
```

Tone and roof change both glyph and **byte length** of a symbol. Splitting on
bytes without re-encoding from `m_buffer` risks mid-code-point cuts. Any future
API must re-encode from symbols, not slice the previous UTF-8 string by LCP.

## 8. Limits

- No public mutability metadata in `UkEngine`.
- Default freeMarking expands rewrite reach.
- Tone position is a function of vowel sequence completeness.
- Restore/replay can replace the entire last word.
- Re-implementing reachability outside the engine violates project rules.
- The forwarded-backspace architecture (forward Backspace to the application
  when backspace crosses the committed frontier) has not been studied. This
  research evaluates only a commit-only model that never edits or forwards
  deletion into committed text.

## 9. Decision

| Option | Result |
| --- | --- |
| A — safe general stable-prefix (commit-only) | **No for commit-only model.** Forward rewrites (CE1–CE4), backspace (CE5), and restore (CE6) break mid-word commit without SurroundingText. A forwarded-backspace model is not evaluated. |
| B — limited safe subset (commit-only) | **No additional subset** beyond "commit only at composition/word boundary", which `PreeditFallbackController` already implements. A mid-word subset with proof would require engine-internal reachability (out of scope / second engine). |
| **C — stop with evidence for the commit-only model** | **Yes.** Do not ship commit-only hybrid stable-prefix. Keep full client preedit without SurroundingText. A forwarded-backspace architecture is outside this research scope. |

### What we still deliver under C

1. This research document.
2. Regression tests that lock the counterexamples and forbid naive LCP oracles.
3. Documentation that commit-only stable-prefix is **not production-ready** and
   is not enabled.
4. No dead experimental path in the Fcitx addon default configuration.

### What would be required to revisit A/B for a commit-only model

- Engine-owned API that returns a **proven** monotonic mutable-start symbol
  index after each key, computed inside UniKey rules (not re-derived outside).
- Explicit contract for backspace and restore relative to that index.
- Exhaustive continuation tests with zero invariant failures.
- Feature flag default OFF until real-app matrix passes.

Until then, **stable_prefix_bytes = 0** is the only honest oracle for a
commit-only model.

## 10. Related docs

- [browser-input-policy.md](browser-input-policy.md) — path selection, preedit vs direct
- [architecture.md](architecture.md) — layering
- [stable-prefix.md](stable-prefix.md) — product-facing decision summary
- [fcitx5-addon.md](fcitx5-addon.md) — addon paths
