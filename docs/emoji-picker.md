<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Optional emoji picker

UniLume's emoji picker is disabled by default. Set `EmojiEnabled=True` in the
Fcitx input-method configuration and use `EmojiHotkey` (default
`Control+Alt+period`) or the status action to open it. The addon asks Fcitx's
installed `emoji` module for the Vietnamese CLDR dictionary, with Fcitx's
English fallback. UniLume does not bundle another emoji database and does not
perform network search.

The module and search index are loaded only on the first explicit picker
request. A missing or disabled Fcitx emoji module leaves the hotkey unconsumed
and the Vietnamese input path unchanged. The index is capped at 16,384
keyword/glyph entries, queries at 64 bytes, and displayed results at 128.
Ranking is deterministic: exact, prefix, word-prefix, substring, then ordered
subsequence. Glyph and keyword order break ties.

The candidate window follows the global Fcitx page size and navigation keys.
Number keys select the visible candidate, arrows move the cursor, the global
page keys change pages, Enter or Space commits the selected emoji, Backspace
edits the query, and Escape closes the picker. Opening, closing, selection,
focus loss and reset all cross the existing composition boundary; the picker
never becomes a second Vietnamese composition owner.

## Local history and privacy

Only committed emoji glyphs are stored. Search text, surrounding text,
application identity and timestamps are never recorded. The most-recently-used
list is capped at 64 unique entries and 64 KiB. It is stored under:

```text
$XDG_DATA_HOME/fcitx5/unilume/emoji-history-v1
```

The file is strict UTF-8 with a version header. Corrupt, duplicate, oversized
or incomplete content is rejected and never replaces the last known-good
in-memory list. Updates and clear-history use a private `0600` temporary file,
`fsync`, atomic rename and parent-directory sync. The `Clear emoji history`
status action replaces the list with an empty valid history.

The picker has no telemetry, network access, background worker or per-key file
I/O. Normal Vietnamese typing does not load the module, build the index, search
it or touch the history file.

## Qualification

The real Fcitx Vietnamese emoji dictionary currently exposes 3,232 bounded
keyword/glyph entries in the integration fixture. Twenty complete searches
finish below the two-second regression ceiling in an unoptimized sanitizer
build. The normal typing path is also measured independently with the picker
disabled: five randomized paired runs of one million events passed correctness,
lifecycle, throughput, p95/p99 latency and peak-RSS gates against the parent
revision. The measured medians were 4.50 million versus 4.32 million keys/s,
374 versus 380 ns p95, 527 versus 528 ns p99, and 23,428 versus 23,488 KiB
peak RSS. These host-specific values are evidence for the regression gate, not
portable performance promises.
