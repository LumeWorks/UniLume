# Core benchmark methodology

UniLume includes a dependency-free C++23 harness for measuring the inherited
engine through the real `ukinterface` API. It does not benchmark a desktop
backend, Lotus, or fcitx5-unikey.

## Build

Performance reports must use the normal Release configuration:

```sh
cmake -S . -B build/benchmarks \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNILUME_BUILD_BENCHMARKS=ON
cmake --build build/benchmarks --parallel 2
ctest --test-dir build/benchmarks --output-on-failure
```

`UNILUME_BUILD_BENCHMARKS` defaults to `OFF`. A normal build does not compile
the harness, allocator hooks, or corpus files, and no benchmark dependency is
downloaded or linked into production targets.

## Run

Run all latency, throughput, allocation, and burst cases:

```sh
build/benchmarks/benchmarks/unilume_core_benchmark
```

Select one corpus or change the measured rounds:

```sh
build/benchmarks/benchmarks/unilume_core_benchmark \
  --corpus=telex \
  --warmup=10 \
  --iterations=100
```

Corpus names are `telex`, `vni`, `viqr`, `urls_and_emails`, `code_like`,
`unicode`, and `mixed`. Burst cases contain exactly 10, 50, 100, and 1,000 key
events without sleeps.

Export machine-readable JSON:

```sh
build/benchmarks/benchmarks/unilume_core_benchmark \
  --format=json \
  --output=benchmark-results.json
```

Local result files named `benchmark-results*.json` or
`benchmark-results*.csv` are ignored by Git.

## Allocation-per-key instrumentation

Allocation measurement is benchmark-only and is never linked into
`unilume_engine`, `unilume_context`, `unilume_core`, or the Fcitx5 addon. GNU
linker wrapping intercepts `malloc`, `calloc`, and `realloc`; benchmark-local
global C++ allocation operators record scalar, array, and nothrow `new` calls.
A thread-local scope is active only around each `UnikeyFilter`,
`UnikeyBackspacePress`, or `UnikeyResetBuf` call. Corpus setup, engine setup,
output mutation, validation, checksum, report construction, and I/O are outside
that scope.

The report contains allocation calls, requested bytes, allocations/key,
bytes/key, and the mean cost of an empty measurement scope. A zero is reported
only after `allocation-instrumentation` verifies a known mixed fixture covering
`malloc`, `calloc`, `realloc`, scalar `new`, and array `new[]`. Calls outside an
active scope must remain invisible.

`realloc` is call-based: a successful non-zero request counts as one allocation
operation and records the requested new size, whether or not the allocator
moves the block. Requested bytes are not live bytes, retained bytes, or RSS.
The instrumentation does not claim attribution inside a shared Fcitx process;
it applies only to the standalone core benchmark executable.

The hooks add measurement overhead and therefore run in a separate allocation
pass. Latency and throughput passes do not activate them. The empty-scope
control quantifies scope bookkeeping but cannot quantify allocator- and
compiler-specific perturbation completely.

## Long-running RSS check

The default long run processes at least one million key events:

```sh
build/benchmarks/benchmarks/unilume_core_benchmark \
  --soak \
  --keys=1000000 \
  --format=json \
  --output=benchmark-results-soak.json
```

The soak result records current RSS from `/proc/self/status`, process maximum
RSS from `getrusage`, ten current-RSS checkpoints, checksum, error count, and
latency drift between the first and second half of checkpoint means. Warm-up
happens before the first checkpoint. The harness reports a linear-growth error
only when growth is both material (at least 1 MiB or 25% of warm RSS) and at
least 80% of checkpoint transitions increase. This is a leak signal, not a
portable absolute memory limit. A full soak also reports a latency-growth error
only when at least 80% of checkpoint transitions increase and the second half
mean is over 25% above the first. CI smoke reports drift without applying that
latency rule.

## Measurement boundaries

- `std::chrono::steady_clock` surrounds only `UnikeyFilter`,
  `UnikeyBackspacePress`, or `UnikeyResetBuf`.
- Allocation scopes surround the same API calls in a separate pass.
- Corpus loading, setup, output mutation, validation, checksum, reporting, and
  console/file I/O are outside each per-key sample.
- Latency reports min, max, arithmetic mean, population standard deviation,
  interpolated p50/p95/p99, sample count, and drift between the first and
  second half of measured round means.
- Throughput sums measured engine-call nanoseconds and reports keys/second,
  corpus rounds/second, total keys, total time, and an FNV-1a checksum.
- Every measured scenario is checked against an explicit expected output and
  strict UTF-8 validation. It runs twice before measurement to detect
  non-deterministic output, then validates every measured iteration.
- UTF-8 input is counted as the byte events accepted by the current C API, not
  as Unicode scalar values.

The URL and code corpora freeze current Telex core output, including
composition inside some strings. They are not claims that UniLume implements a
URL mode or code mode.

## Sanitizer smoke

The `Benchmark Smoke` workflow builds the harness in Debug with ASan and UBSan
using GCC and Clang. It runs the mixed C/C++ allocation fixture, all corpora,
burst sizes, and a 10,000-key soak. It validates correctness and memory safety
only. It has no latency or throughput threshold and is not a required
performance gate.

The equivalent local command is:

```sh
cmake -S . -B build/benchmark-smoke \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUNILUME_BUILD_BENCHMARKS=ON \
  -DUNILUME_ENABLE_ASAN=ON \
  -DUNILUME_ENABLE_UBSAN=ON
cmake --build build/benchmark-smoke --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/benchmark-smoke --output-on-failure \
  -R 'allocation-instrumentation|benchmark-smoke'
```

## Comparison limits

Nanosecond and allocation results are sensitive to allocator, standard library,
compiler, optimization, and operating system. Compare results only on the same
machine with the same commit, corpus, compiler, build type, options, allocator,
and measurement protocol. A Debug or sanitizer run is not a performance
baseline. Core-only numbers cannot support claims that UniLume is faster than
Lotus or fcitx5-unikey; those projects require direct, equivalent measurements.
