<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Production macro contract

UniLume expands validated, context-local UTF-8 macros through the inherited
UniKey engine. Macro parsing and file access happen only when configuration is
loaded or reloaded. Key processing reads an installed fixed-capacity table and
performs no macro file I/O.

## Behavior

- An exact key expands when followed by a word boundary such as whitespace or
  punctuation.
- Matching is case-sensitive. Define separate entries such as `vn` and `VN`
  when different capitalization is required.
- Expansion output is one engine edit and is not fed back through the matcher,
  so entries cannot recurse.
- Shift+Space retains the inherited behavior and does not trigger a macro.
- Reload is a composition boundary. Invalid reloads preserve the last
  known-good table and cannot partially update another input context.

Dynamic placeholders, scripts, shell commands and arbitrary templates are not
supported.

## Canonical file format

The canonical UTF-8 format begins with:

```text
unilume_macro_version=1
```

Each following line contains `key`, one tab, and `replacement`. Backslash,
tab, newline and carriage return inside a field are encoded as `\\`, `\t`,
`\n` and `\r`. Unknown escapes, empty fields, duplicate keys, invalid UTF-8
and malformed lines are rejected.

Limits match the inherited engine:

- 1,024 entries;
- 15 Unicode scalar values per key;
- 1,023 Unicode scalar values per replacement;
- 128 KiB total converted engine storage;
- 4 MiB serialized input.

The serializer preserves entry order. Saves use a mode-0600 temporary file,
`fsync`, atomic rename and parent-directory sync.

## Import and migration

UniLume imports the historical UTF-8 format with a `version=1` header and
`key:text` lines, and headerless VIQR `key:text` files. An imported table is
marked as migrated. The Fcitx addon writes canonical form atomically only
after the complete legacy file validates; corrupt input is never overwritten.

## Fcitx5 configuration

`MacroEnabled` enables the feature and `MacroFile` selects the table. Enabling
with an empty, missing or invalid file is rejected without replacing active
configuration. Each input context applies a new table at a safe composition
boundary and otherwise compares only a generation counter per key.
