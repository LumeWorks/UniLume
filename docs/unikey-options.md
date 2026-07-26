<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# UniKey option contract

Issue #42 exposes only inherited UniKey behavior that has an observable,
per-context effect through the production event API. The adapter maps one
immutable `config::Snapshot` to `UlEngineOptions`; it never assigns fields in
`UkSharedMem` directly and it does not reimplement any typing rule.

| Public setting | Legacy field | Default | Golden behavior |
| --- | --- | --- | --- |
| `spell_check` | `spellCheckEnabled` | on | constrained `ues ` remains literal; disabling accepts `úe ` |
| `free_marking` | `freeMarking` | on | `dad` can rewrite the earlier `d` to `đa`; constrained mode keeps `dad` |
| `modern_tone` | `modernStyle` | off | legacy `hoas ` gives `hóa `; modern placement gives `hoá ` |
| `auto_restore` | `autoNonVnRestore` | on | `wikipedia ` restores its raw keys; disabling retains `ưikipedia ` |

All four values are closed booleans. The C API rejects any value other than
zero or one without changing the active context. Setting an accepted option
snapshot resets only that engine context. In the Fcitx adapter, a reload is a
composition boundary: pending preedit text is committed first, both controller
paths reset, and the next composition uses the new snapshot. Other input
contexts retain their own engine and option state.

## Inventory of non-exposed legacy fields

- `macroEnabled` and `alwaysMacro` require a validated macro table lifecycle;
  they remain deferred to Issue #43 rather than appearing as no-op controls.
- `useUnicodeClipboard` belongs to historical clipboard integrations, not the
  UTF-8 commit API.
- `useIME` is marked Win32-only in the inherited source.
- `strictSpellCheck` is not applied by the inherited public option setter and
  has no independent production contract in this repository.

Adding any of these requires a separate behavioral contract and positive and
negative corpus. Persisted schema fields alone are not evidence that an option
is safe to expose.
