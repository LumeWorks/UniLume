<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Configuration schema v2

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

On a missing config the Store uses v2 defaults. On a malformed, duplicate,
unknown or future-version config it returns a diagnostic and preserves the last
known-good in-memory snapshot. It never silently overwrites a corrupt file.

## Canonical v2 format

The format is strict UTF-8 `key=value` lines with ASCII keys. No quoting,
comments after values, duplicate keys, unknown keys or implicit coercions are
accepted. Every listed key is required; a partial file is rejected. The
serializer always writes this order:

```ini
schema_version=2
input_method=telex
output_charset=utf8
spell_check=true
free_marking=true
modern_tone=false
auto_restore=true
macro_enabled=false
macro_file=
```

Stable identifiers are lowercase ASCII: input methods `telex`, `vni`, `viqr`;
the only currently exposed output charset is `utf8`. Future fields for hotkeys,
application rules and dictionaries require a schema-versioned Issue;
they must not be invented by individual adapters.

## Migration and persistence

Version 0 is the v1 scalar set without `schema_version`; version 1 is that
same set with its explicit version. Loading either produces a v2 snapshot
with an empty `macro_file`, marked as migrated. Any unsupported version is
rejected.
The v0, v1 and corrupt-file fixtures are regression inputs under
`tests/config/fixtures/`.

Save validates first, writes a `mkstemp` file in the destination directory with
mode `0600`, writes and `fsync`s it, atomically renames it over the destination,
then syncs the parent directory where supported. A crash can leave a temporary
file, but can never replace the previous config with a partial file. Temporary
files are ignored on load.

## Scope boundary

v2 deliberately does not persist GUI widgets, Fcitx actions, custom keymaps,
dictionary paths, legacy charsets, hotkeys or app policy. Those
features are consumers of this contract in their own Issues. The schema does
not change the UniKey algorithm or its defaults.

The validated runtime/file contract for custom maps now lives in
[keymap-support.md](keymap-support.md). Persisting its path remains a future
schema migration owned by the configuration GUI/backup work; adapters may not
invent private config keys.

## Fcitx5 input-method configuration

The Fcitx5 addon exposes the same method choice per Fcitx input-method entry:
`Telex`, `VNI`, or `VIQR`. Each Fcitx input context owns separate composing
state; applying a changed method at the next event forms a composition
boundary, commits pending preedit text, and resets both controllers before the
new method is used. The only exposed output charset is `UTF8`. Legacy encodings
are deliberately rejected until their complete conversion and Fcitx commit
round-trip have been demonstrated.

`spell_check`, `free_marking`, `modern_tone`, and `auto_restore` map one-for-one
to existing UniKey option fields. A reload is
applied per input context at the next event as a composition boundary: pending
preedit commits, both controllers reset, and then the full immutable snapshot
is installed. Invalid option values are rejected without replacing the active
context snapshot.

`macro_enabled` and `macro_file` map to the validated lifecycle documented in
[macro-support.md](macro-support.md). Enabling macros with an empty, missing
or invalid table is rejected while preserving the active snapshot.

The complete mapping, defaults, golden behavior, and deliberately deferred
legacy fields are recorded in [unikey-options.md](unikey-options.md).
