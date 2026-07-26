// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "benchmark_result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace unilume::integration_benchmark {

struct ResourceCountMetrics {
    std::size_t initial{};
    std::size_t final{};
    std::size_t maximum{};
    bool linear_growth_detected{};
};

struct StabilityCheckpoint {
    std::size_t keys{};
    std::uint64_t current_rss_kib{};
    std::size_t open_file_descriptors{};
    std::size_t threads{};
    double p99_latency_ns{};
};

struct IntegrationResult {
    std::string name;
    std::uint64_t total_keys{};
    double total_seconds{};
    double keys_per_second{};
    benchmark::LatencyStatistics latency;
    double latency_drift_percent{};
    double p99_latency_drift_percent{};
    bool p99_latency_growth_detected{};
    double process_cpu_seconds{};
    double process_cpu_utilization_percent{};
    std::uint64_t checksum{};
    std::uint64_t errors{};
    std::uint64_t lost_events{};
    std::uint64_t duplicate_events{};
    std::uint64_t reordered_events{};
    std::size_t max_queue_depth{};
    std::size_t final_queue_depth{};
    std::uint64_t completed_transactions{};
    std::uint64_t aborted_transactions{};
    std::uint64_t stale_callbacks{};
    std::uint64_t duplicate_preventions{};
    std::uint64_t reset_count{};
    std::uint64_t queue_overflow_count{};
    bool pending_transaction{};
    benchmark::RssMetrics rss;
    ResourceCountMetrics open_file_descriptors;
    ResourceCountMetrics threads;
    std::vector<StabilityCheckpoint> stability_checkpoints;
};

struct IntegrationReport {
    benchmark::ReportMetadata metadata;
    std::string allocation_measurement{"not_measured"};
    std::vector<IntegrationResult> results;
};

} // namespace unilume::integration_benchmark
