// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace unilume::benchmark {

struct LatencyStatistics {
    std::size_t samples{};
    double min_ns{};
    double max_ns{};
    double mean_ns{};
    double stddev_ns{};
    double p50_ns{};
    double p95_ns{};
    double p99_ns{};
};

struct RssCheckpoint {
    std::uint64_t keys{};
    std::uint64_t current_kib{};
};

struct RssMetrics {
    std::uint64_t initial_kib{};
    std::uint64_t after_warmup_kib{};
    std::uint64_t final_kib{};
    std::uint64_t maximum_kib{};
    bool linear_growth_detected{};
    std::vector<RssCheckpoint> checkpoints;
};

struct AllocationMetrics {
    std::uint64_t total_allocations{};
    std::uint64_t allocated_bytes{};
    double allocations_per_key{};
    double bytes_per_key{};
    double empty_scope_overhead_ns{};
};

struct BenchmarkResult {
    std::string name;
    std::uint64_t total_keys{};
    std::size_t iterations{};
    double total_seconds{};
    double keys_per_second{};
    double iterations_per_second{};
    std::uint64_t checksum{};
    std::size_t errors{};
    double latency_stability_drift_percent{};
    bool latency_growth_detected{};
    LatencyStatistics latency;
    bool has_rss{};
    RssMetrics rss;
    bool has_allocations{};
    AllocationMetrics allocations;
};

struct ReportMetadata {
    std::string commit;
    std::string compiler;
    std::string build_type;
    std::string cpu_model;
    std::string timestamp_utc;
    std::string allocation_measurement{"benchmark_scoped_c_cpp_interposition"};
};

struct BenchmarkReport {
    ReportMetadata metadata;
    std::vector<BenchmarkResult> results;
};

} // namespace unilume::benchmark
