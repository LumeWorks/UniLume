# Per-application input policy

Issue #46 adds one deterministic policy table for selecting UniLume's input
path from the Fcitx `InputContext::program()` identity. The decision remains
inside the existing `InputContextState`; it does not introduce another text
writer or backend.

## Modes

- `automatic`: use direct replacement only when the current Fcitx context
  advertises the required surrounding-text capability, otherwise use preedit;
- `direct`: request the approved direct replacement backend, with an immediate
  safe-preedit fallback when that capability is unavailable;
- `safe-preedit`: always use the existing client/server preedit fallback;
- `off`: pass ordinary key events through without running the engine.

An empty application identity always resolves to `safe-preedit`. A mode,
identity, rule, or policy-generation change is a composition boundary: pending
preedit is committed, both controllers and the backend are reset, and the next
key starts with one owner.

## File format

The ASCII policy is tab-separated and canonical:

```text
unilume_app_policy_version=1
default	automatic
rule	org.example.editor	direct
rule	org.example.*	safe-preedit
```

Exactly one default is required. Rules are either exact identities or a single
trailing `*` prefix. Exact matches outrank prefixes; the longest prefix wins.
Duplicate rules, middle wildcards, unknown modes, more than 4,096 rules, an
identity longer than 128 bytes, or a file larger than 1 MiB are rejected.
Parsing and file I/O happen during configuration reload; key handling resolves
against an immutable snapshot.

Set `ApplicationPolicyEnabled=True` and `ApplicationPolicyFile` to enable the
table. A missing, unreadable, or invalid replacement preserves the active
configuration and snapshot.

## Selection and status

The Fcitx status-area action shows the requested and effective mode, including
the safe fallback when direct replacement is unavailable. Its menu selects any
of the four modes for the current input context. The default cycle hotkey is
`Control+Alt+u`; `AutomaticModeHotkey`, `DirectModeHotkey`,
`SafePreeditModeHotkey`, and `OffModeHotkey` are optional. Invalid or
conflicting hotkeys reject the complete update.

The cycle order is automatic, direct, safe-preedit, off. A focus reset clears
the temporary selection; an identity or policy reload also returns the context
to its resolved rule.
