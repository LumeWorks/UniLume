<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Integration testing

UniLume tests the C++23 direct-commit controller without a desktop session.
The deterministic backend uses a virtual event counter; it never sleeps and
does not depend on scheduler timing.

## Build and test

```sh
cmake -S . -B build/integration -DCMAKE_BUILD_TYPE=Debug
cmake --build build/integration --parallel 2
ctest --test-dir build/integration --output-on-failure
```

CTest separates engine regression from these integration suites:

- `integration-immediate`;
- `integration-delayed`;
- `integration-duplicate`;
- `integration-transaction`;
- `integration-preedit-fallback`;
- `integration-mode-policy`;
- `integration-burst`;
- `integration-soak-smoke`;
- `integration-stability-recovery`;
- `integration-zero-preedit-architecture`;
- `integration-zero-preedit-soak`.

The harness covers immediate replacement, 1/2/5/10/50-event delay, missing or
stale surrounding text, invalid surrounding UTF-8, cursor mismatch,
delete/commit failure, duplicate and reordered callback, dropped callback,
focus reset, safe preedit fallback, path selection, and bounded burst input.
The fallback suite repeats the Firefox/Chrome corpus to detect duplicated,
lost, or reordered prefixes. Delay is virtual; fault injection is
deterministic and repeatable.

The zero-preedit architecture suite locks the one-owner decision and contains
executable cursor/focus/selection and dropped/reordered-update
counterexamples. Its 1/2/5 ms profiles cover 1,000 and 10,000 events. To pace
those intervals against the monotonic wall clock, run:

```sh
UNILUME_PROTOTYPE_WALL_BURST=1 \
  build/integration/tests/unilume_integration_tests \
  zero-preedit-architecture
```

The default soak is a short CI smoke. The acceptance soak stays in one process
for 30 minutes and samples RSS:

```sh
UNILUME_PROTOTYPE_SOAK_SECONDS=1800 \
  build/integration/tests/unilume_integration_tests zero-preedit-soak
```

The stability-recovery suite repeatedly destroys and recreates the real
controller/test-backend pair, injects replacement refusal, dropped completion
and uncertain cancellation, and verifies exact post-recovery output and a
drained queue. Its default is 10,000 events. A deterministic acceptance run is:

```sh
UNILUME_RECOVERY_SOAK_EVENTS=1000000 \
  build/integration/tests/unilume_integration_tests stability-recovery
```

`engine-allocation-failure` calls the public context API while the test-only
global nothrow allocation boundary returns null. It requires
`UL_STATUS_OUT_OF_MEMORY`, a null result, and successful context creation
immediately afterwards. Production code is not given a test allocator.

Every suite checks final output, valid UTF-8, bounded/final queue depth, and
the absence of a pending transaction. Duplicate and reordered callbacks must
be rejected by sequence ID. The sustained delayed profile is five virtual
events. Longer delays are exercised as finite bursts because no finite queue
can absorb an indefinitely faster producer.

## Integration benchmark

The benchmark target is optional and uses the normal Release flags:

```sh
cmake -S . -B build/integration-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNILUME_BUILD_INTEGRATION_BENCHMARKS=ON
cmake --build build/integration-benchmark --parallel 2

build/integration-benchmark/benchmarks/unilume_integration_benchmark \
  --keys=1000 \
  --profile=all
```

Profiles are `immediate`, `delayed`, and `stale`. One-million and ten-million
event qualifications and JSON export use:

```sh
build/integration-benchmark/benchmarks/unilume_integration_benchmark \
  --keys=1000000 \
  --profile=all \
  --format=json \
  --output=integration-benchmark-results.json
```

Repeat with `--keys=10000000` for the release-candidate long run.

The report includes per-event p50/p95/p99, mean and standard deviation,
throughput, mean and checkpoint-p99 drift, process CPU, queue metrics,
transaction counts, current/peak RSS, FD/thread bounds, resource checkpoints,
checksum, and lost/duplicate/reordered error counters. At one million events
or more, sustained RSS/FD/thread growth or p99 growth over the 25% SLO is an
error. Local result files remain untracked.

Allocation count is `not_measured`. RSS is not an allocation count, and
Issue #16 tracks instrumentation that can cover both C and C++ paths without
distorting the hot path.

The integration numbers measure the controller plus deterministic backend,
not GUI rendering, Fcitx IPC, Lotus, or fcitx5-unikey. They do not establish a
performance comparison with another input method.
