# Per-application input policy

The Fcitx addon exposes four behavior modes:

- `automatic`: verified atomic replacement, otherwise native passthrough;
- `direct`: explicit direct/uinput compatibility path;
- `safe-preedit`: recognizable underlined composition;
- `off`: leave ordinary keys to the application.

Automatic never falls back to preedit or uinput. If atomic replacement is not
available, the original event is not filtered. An empty application identity
resolves to Automatic.

## File format and compatibility

The version-1 ASCII policy remains tab-separated:

```text
unilume_app_policy_version=1
default	automatic
rule	org.example.editor	direct
rule	org.example.*	off
```

Exactly one default is required. Exact identities outrank trailing-`*`
prefixes and the longest prefix wins. All four modes round-trip without a
migration; existing Direct/Off policy files remain valid.

Duplicate rules, middle wildcards, unknown modes, more than 4,096 rules, an
identity longer than 128 bytes, or a file larger than 1 MiB are rejected.
Parsing and file I/O occur only on configuration reload.

## Selection and status

The status menu and cycle expose Automatic, Direct, Safe preedit and Off.
Their existing dedicated hotkeys select the matching mode.

Automatic reports `Atomic direct` or `Atomic replacement unavailable`.
Direct reports its Fast/Guarded strategy. Focus and policy changes fence
pending replacement state before the next key.

`VerifiedDirectEnabled` defaults to `True`; setting it to `False` is the
immediate rollback to safe preedit.
Neither an application rule nor a mode hotkey can bypass it. See
[verified-direct-backend.md](verified-direct-backend.md).
