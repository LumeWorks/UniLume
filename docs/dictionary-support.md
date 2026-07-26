<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Personal dictionary

The personal dictionary extends invalid-word restoration without changing the
UniKey state machine. Its policy is defined in
[ADR 0002](adr/0002-personal-dictionary-policy.md).

## File format

The canonical UTF-8 file starts with:

```text
unilume_dictionary_version=1
```

Each non-comment entry is a behavior, one tab, and a word:

```text
keep	úe
restore	wikipedia
restore	GitHub123
```

`keep` words are exact-case ASCII or precomposed Latin/Vietnamese letters and
digits. Combining marks and Unicode punctuation are rejected. `restore` words
contain only reachable ASCII letters and digits; they are stored and matched
case-insensitively while output preserves the bytes the user typed. Duplicate,
decomposed, malformed, oversized and unsupported-version input is rejected
with line and field information.

The limits are 65,536 entries, 128 bytes per word and 8 MiB serialized input.
Save uses a private mode-0600 temporary file, `fsync`, atomic rename and parent
directory sync. Failed reload preserves the active immutable snapshot.

## Examples and counterexamples

- With `keep	úe`, typing `ues ` produces `úe ` even when the inherited spell
  policy considers the token invalid.
- With `restore	as`, typing `as ` produces `as ` rather than `á `.
- If `vn` is both a macro key and a restore word, the macro expands.
- `http://as.example`, `user@example.com`, quoted text and code literal mode
  are not rewritten by dictionary policy.
- `bad-word` is not a valid restore entry because punctuation is a boundary.
- A decomposed `u` plus combining acute accent is rejected; write the
  precomposed form instead.

Fcitx exposes `DictionaryEnabled` and `DictionaryFile`. Enabling a missing or
invalid file is rejected atomically. Each input context receives the new
snapshot at a composition boundary; other contexts remain unchanged.
