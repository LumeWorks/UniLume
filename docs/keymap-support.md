<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Custom keymap contract

Issue #44 exposes the inherited UniKey user-keymap capability without using
the legacy file loader in production. The legacy loader performs file I/O,
writes diagnostics to stderr and can return a partially accepted table.
UniLume instead decodes the complete text into an immutable
`keymap::Snapshot`, validates it, and only then activates a replacement table
on one engine context.

## Format

The accepted format is compatible with the useful subset of the historical
UniKey mapping:

```text
; comments start with semicolon
q = Tone1
f = Tone2
x = Tone3
```

Keys are one printable, unmodified ASCII character. Whitespace, `=`, `;`,
control keys and modifier expressions such as `Ctrl+q` are rejected.
Alphabetic mappings are case-insensitive, so defining both `q` and `Q` is a
conflict. Duplicate keys, unknown actions, empty files, more than 64 entries
and files over 64 KiB are rejected with line and field information.

Supported legacy action labels are `Tone0` through `Tone5`, `Roof-All`,
`Roof-A`, `Roof-E`, `Roof-O`, `Hook-Bowl`, `Hook-UO`, `Hook-U`, `Hook-O`,
`Bowl`, `D-Mark`, `Telex-W` and `Escape`.

## Lifecycle and rollback

Parsing and file I/O happen outside the key path. Preview uses `decode()` and
`validate()` without touching an engine. `ul_engine_set_keymap()` builds a
complete temporary 256-entry table and swaps it into a single context only
after every entry passes validation. A rejected activation preserves the
last-known-good table. Calling `ul_engine_set_input_method()` reverts that
context to a built-in Telex, VNI or VIQR map.

The C++ controllers treat activation and revert as composition barriers:
pending replacement/preedit state is reset before the next key. Other input
contexts are independent. The parser is included in the normal property and
libFuzzer parser target.

Persistence and the editor UI consume this contract in the configuration GUI
issue; they must not bypass it or call the historical loader.
