# Issue #55 performance profile

This record describes the profile-guided work performed at
`c7ba915c5c4b37f29566e25c410e03eba8dd8fa1`. It is intentionally a compact,
reviewable artifact rather than a large Callgrind dump tied to one machine.

## Host and method

- Debian 13, GCC 14.2, Intel Core i3-4160, Fcitx5 5.1.12.
- Release, RelWithDebInfo, and `-pg` builds used the same source revision.
- Core used the mixed corpus. Integration used the immediate backend with
  1,000,000 events.
- Callgrind and gprof were both used so optimization was not based on a single
  sampler. A separate Callgrind run launched the real Fcitx process with the
  RelWithDebInfo UniLume module and replayed approximately 150 Chrome key
  events.

Reproduction:

```sh
cmake -S . -B build/profile \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DUNILUME_BUILD_BENCHMARKS=ON \
  -DUNILUME_BUILD_INTEGRATION_BENCHMARKS=ON
cmake --build build/profile --parallel 2
valgrind --tool=callgrind --callgrind-out-file=core.callgrind \
  build/profile/benchmarks/unilume_core_benchmark \
  --corpus=mixed --iterations=10000 --warmup=100 --format=json
valgrind --tool=callgrind --callgrind-out-file=integration.callgrind \
  build/profile/benchmarks/unilume_integration_benchmark \
  --profile=immediate --keys=1000000 --format=json
```

## Findings and decisions

| Path | Profile evidence | Decision |
| --- | ---: | --- |
| inherited `UkEngine::process` | 15.99% direct Callgrind cost; 20.18% gprof | Keep unchanged: Issue #55 does not authorize a UniKey algorithm change. |
| `DirectCommitController::processNow` | 70.97% inclusive Callgrind cost | Retain transaction validation and fallback semantics. |
| `DirectCommitController::startTransaction` | 33.87% inclusive Callgrind cost | Retain bounded copies because an asynchronous backend owns the transaction after submit returns. |
| Fcitx mode synchronization | 157 capability observations in the approximately 150-key live Chrome run | Reuse that observation for mode selection and diagnostics instead of observing twice per preedit event. |
| Fcitx surrounding snapshot validation | source event path performed one scan for mode selection and another for direct replacement | Reuse one fully validated snapshot with a metadata-matched, single-use ticket in the same synchronous event. |
| ASCII boundary classification | candidate A/B median throughput regressed | Rejected and removed before commit. |

The accepted Fcitx change reduces full surrounding-text validation from two
scans to one on a direct key. Preedit diagnostics also reuse the capability
observation already made for mode selection. The ticket is allocation-free,
is invalidated by reset or snapshot metadata changes, and can be consumed only
once. The actual replacement request still rejects missing capability,
invalid UTF-8, selection, invalid cursor, oversized text, and deletion beyond
the cursor.

## A/B and budgets

Five alternating Release A/B rounds showed no correctness, queue, RSS-growth,
or lifecycle regression. The integration simulation does not contain the
Fcitx snapshot scan, so its timing is used as a regression control rather than
as evidence for the Fcitx optimization. The checked-in CI gate repeats that
control on the same runner and enforces:

- zero errors, loss, duplication, reorder, pending transaction, and RSS
  growth;
- p95, p99, and throughput relative budgets calibrated from median absolute
  deviation and capped at 15%;
- candidate peak RSS no more than 1 MiB above the baseline;
- at least five randomized, paired rounds.

The live X11 Chrome harness ran five alternating UniLume/Lotus rounds after
adding explicit focus, switch warm-up, and composition reset between samples.
On the four-scenario Telex subset UniLume produced 0/20 incorrect samples while
packaged Lotus 3.4.0 produced 8/20 at a 20 ms injection delay. UniLume's paired
p50 completion ratio was 0.736, but p95 was 1.059 and p99 was 1.302. The strict
desktop gate therefore remained false: timeout-inflated reference samples are
not counted as a fair speed win, and the paired tail did not clear the 5%
margin. The raw JSON was retained as issue evidence rather than committed
because it contains host-specific process metadata.

Use the following commands for future regression and desktop gates:

```sh
python3 scripts/benchmark/check_integration_regression.py \
  --baseline /path/to/base/unilume_integration_benchmark \
  --candidate /path/to/head/unilume_integration_benchmark \
  --rounds 5 --keys 1000000 --output regression.json

python3 scripts/benchmark/compare_fcitx5_desktop.py \
  --candidate unilume --reference lotus \
  --window-id WINDOW --token TOKEN --rounds 5 \
  --switch-settle-milliseconds 300 \
  --output desktop.json --enforce-slo
```
