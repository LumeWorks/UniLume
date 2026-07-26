<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Configuration schema v1

Issue #40 establishes the only persistent configuration boundary for UniLume.
The config library owns parsing, validation, migration and persistence. Engine,
Fcitx, CLI and a future GUI consume an immutable `Snapshot`; none may parse or
write a settings file in the typing path.

## Ownership and lifecycle

One `config::Store` belongs to the integration owner (initially the Fcitx
addon). It loads a snapshot at startup, then may reload only at an event-loop
boundary. A context copies its applied snapshot before it processes a key.
Changing a snapshot is therefore never an in-place mutation visible to an
active composition: the integration owner must commit or reset that
composition, install the new snapshot, then create/apply the next context
state. File I/O and parsing are forbidden per key.

On a missing config the Store uses v1 defaults. On a malformed, duplicate,
unknown or future-version config it returns a diagnostic and preserves the last
known-good in-memory snapshot. It never silently overwrites a corrupt file.

## Canonical v1 format

The format is strict UTF-8-compatible ASCII `key=value` lines. No quoting,
comments after values, duplicate keys, unknown keys or implicit coercions are
accepted. Every listed key is required; a partial file is rejected. The
serializer always writes this order:

```ini
schema_version=1
input_method=telex
output_charset=utf8
spell_check=true
free_marking=true
modern_tone=false
auto_restore=true
macro_enabled=false
```

Stable identifiers are lowercase ASCII: input methods `telex`, `vni`, `viqr`;
the only currently exposed output charset is `utf8`. Future fields for hotkeys,
application rules, dictionaries and macros require a schema-versioned Issue;
they must not be invented by individual adapters.

## Migration and persistence

Version 0 is the same set of v1 fields without `schema_version`; loading it
produces a v1 snapshot marked as migrated. Any other version is rejected.
The v0, v1 and corrupt-file fixtures are regression inputs under
`tests/config/fixtures/`.

Save validates first, writes a `mkstemp` file in the destination directory with
mode `0600`, writes and `fsync`s it, atomically renames it over the destination,
then syncs the parent directory where supported. A crash can leave a temporary
file, but can never replace the previous config with a partial file. Temporary
files are ignored on load.

## Scope boundary

v1 deliberately does not expose GUI widgets, Fcitx actions, custom keymaps,
macro tables, dictionary paths, legacy charsets, hotkeys or app policy. Those
features are consumers of this contract in their own Issues. The schema does
not change the UniKey algorithm or its defaults.
