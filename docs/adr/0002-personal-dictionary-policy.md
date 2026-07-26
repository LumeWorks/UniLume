<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# ADR 0002: Personal dictionary and restoration policy

Status: accepted for Issue #45.

## Decision

UniLume uses one immutable, context-local snapshot containing two sorted word
sets:

- `keep` contains exact, precomposed UTF-8 output words that the user declares
  valid. At a word boundary UniLume keeps the composed token instead of letting
  the inherited invalid-word rule restore its raw keystrokes.
- `restore` contains ASCII letter/digit keystroke tokens. Matching is ASCII
  case-insensitive and restores the exact casing that was typed.

The policy runs only at a word boundary. Binary search is deterministic and
does not allocate. Parsing, sorting, duplicate detection and file access occur
only during load/reload. A context compares a generation counter on each Fcitx
event and crosses a composition barrier only when that generation changes.

Precedence is fixed:

1. URL, email, quoted text and code literal modes bypass the dictionary.
2. An exact macro key wins over a dictionary entry.
3. `restore` wins over `keep` when processing raw input.
4. Otherwise the inherited spell-check and auto-restore behavior applies.

`keep` is exact-case because changing capitalization of Vietnamese output is a
content decision. `restore` is ASCII-insensitive because it returns the user's
original bytes and foreign product names commonly vary in case.

## Unicode and boundaries

The v1 file contract accepts ASCII letters/digits and precomposed Latin or
Vietnamese letters; it rejects combining mark sequences and Unicode
punctuation. This gives one stable representation for Vietnamese entries
without adding a normalization library to the per-key core. Canonically
decomposed input is rejected with a field/line error instead of being compared
inconsistently. `restore` entries are restricted to reachable ASCII
letters/digits.

Whitespace and punctuation are boundaries. The existing literal detector runs
first, so URL/email/code punctuation cannot accidentally invoke restoration.

## Rejected alternatives

- File lookup or a mutable hash table on each key: unpredictable latency and
  unsafe reload ownership.
- Statistical autocorrect, cloud dictionaries or telemetry: outside the
  product and privacy scope.
- Modifying UniKey spelling tables: would change the inherited algorithm.
- Silently accepting decomposed Unicode without full normalization: creates
  entries that appear equal but compare differently.

## Resource budgets

v1 is bounded to 65,536 entries, 128 UTF-8 bytes per word and an 8 MiB file.
The Release regression gate uses the maximum entry count and requires zero
lookup allocations, at most 16 MiB retained upper-bound storage and a 10 µs
p99 lookup ceiling. These are safety ceilings, not comparative Lotus evidence.
