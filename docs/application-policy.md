# Per-application input policy

The Fcitx addon exposes exactly two behavior modes:

- `direct`: run the UniKey pipeline and apply committed replacement text;
- `off`: leave every ordinary key to the application.

Direct never falls back to client or server preedit. If the configured direct
backend is disabled, unavailable, loses capability, or fails before mutation,
the effective path is Off and the original event is not filtered. An empty
application identity resolves to Direct.

## File format and compatibility

The version-1 ASCII policy remains tab-separated:

```text
unilume_app_policy_version=1
default	direct
rule	org.example.editor	direct
rule	org.example.*	off
```

Exactly one default is required. Exact identities outrank trailing-`*`
prefixes and the longest prefix wins. The parser retains compatibility with
old files by mapping `automatic` to `direct` and `safe-preedit` to `off`, and
logs that legacy mapping once per addon instance. Serialization always emits
only `direct` and `off`.

Duplicate rules, middle wildcards, unknown modes, more than 4,096 rules, an
identity longer than 128 bytes, or a file larger than 1 MiB are rejected.
Parsing and file I/O occur only on configuration reload.

## Selection and status

The status menu and cycle expose Direct and Off only. `Control+Alt+u` cycles
between them. `DirectModeHotkey` and `OffModeHotkey` select them directly.
Legacy `AutomaticModeHotkey` selects Direct and `SafePreeditModeHotkey`
selects Off so existing configuration continues to load.

The status reports `Direct - Fast`, `Direct - Guarded`, or
`Direct unavailable - Passthrough`. A focus or policy change fences pending
replacement state before the next key.

`VerifiedDirectEnabled=False` disables every direct backend and therefore
makes Direct pass keys through. `DirectStrategy=Fast|Guarded` selects the
split-transport transaction boundary; it is global backend configuration, not
a third mode. See [verified-direct-backend.md](verified-direct-backend.md).
