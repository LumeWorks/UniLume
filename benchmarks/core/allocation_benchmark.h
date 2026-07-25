// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "benchmark_options.h"
#include "benchmark_result.h"
#include "benchmark_types.h"

namespace unilume::benchmark {

class EngineFixture;

BenchmarkResult runAllocationBenchmark(EngineFixture &engine,
                                       const Corpus &corpus,
                                       const BenchmarkOptions &options);

} // namespace unilume::benchmark
