// SPDX-License-Identifier: GPL-2.0-or-later

#include "allocation_benchmark.h"

#include "allocation_instrumentation.h"
#include "checksum.h"
#include "correctness.h"
#include "engine_fixture.h"

namespace unilume::benchmark {

BenchmarkResult runAllocationBenchmark(EngineFixture &engine,
                                       const Corpus &corpus,
                                       const BenchmarkOptions &options)
{
    for (std::size_t iteration = 0;
         iteration < options.warmup_iterations;
         ++iteration) {
        for (const Scenario &scenario : corpus.scenarios) {
            validateObservation(corpus, scenario, engine.run(scenario, false));
        }
    }

    Checksum checksum;
    std::uint64_t total_keys = 0;
    std::uint64_t allocations = 0;
    std::uint64_t allocated_bytes = 0;
    for (std::size_t iteration = 0; iteration < options.iterations;
         ++iteration) {
        for (const Scenario &scenario : corpus.scenarios) {
            const AllocationObservation observation =
                engine.runAllocations(scenario);
            validateOutput(corpus, scenario, observation.output);
            total_keys += observation.events;
            allocations += observation.allocations;
            allocated_bytes += observation.allocated_bytes;
            checksum.add(observation.output);
            checksum.add(iteration);
        }
    }

    BenchmarkResult result;
    result.name = "allocations/" + corpus.name;
    result.total_keys = total_keys;
    result.iterations = options.iterations;
    result.checksum = checksum.value();
    result.has_allocations = true;
    result.allocations.total_allocations = allocations;
    result.allocations.allocated_bytes = allocated_bytes;
    if (total_keys != 0) {
        result.allocations.allocations_per_key =
            static_cast<double>(allocations) / static_cast<double>(total_keys);
        result.allocations.bytes_per_key =
            static_cast<double>(allocated_bytes) /
            static_cast<double>(total_keys);
    }
    result.allocations.empty_scope_overhead_ns =
        allocation::measureEmptyScopeOverheadNanoseconds();
    if (result.allocations.empty_scope_overhead_ns < 0.0) {
        ++result.errors;
    }
    return result;
}

} // namespace unilume::benchmark
