// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_benchmark_runner.h"

#include "checksum.h"
#include "integration_session.h"
#include "process_resource_reader.h"
#include "rss_reader.h"
#include "statistics.h"
#include "system_metadata.h"
#include "utf8_validation.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <vector>

namespace unilume::integration_benchmark {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view corpus{
    "tieengs Vieetj http://abc.com/a1 user@example.com "
    "std::vector<int> value_2 日本語 한국어 中文 🚀 "};

std::size_t codePointLength(std::string_view text)
{
    const auto lead = static_cast<unsigned char>(text.front());
    if (lead <= 0x7f) {
        return 1;
    }
    if (lead <= 0xdf) {
        return 2;
    }
    if (lead <= 0xef) {
        return 3;
    }
    return 4;
}

class CorpusCursor {
public:
    std::string_view next()
    {
        if (offset_ == corpus.size()) {
            offset_ = 0;
        }
        const std::size_t length = codePointLength(corpus.substr(offset_));
        const std::string_view result = corpus.substr(offset_, length);
        offset_ += length;
        return result;
    }

private:
    std::size_t offset_{};
};

integration::test::BackendProfile profileFor(
    std::string_view name,
    std::size_t keys)
{
    integration::test::BackendProfile profile;
    if (name == "delayed") {
        profile.delay_events = 5;
    } else if (name == "stale") {
        profile.stale_surrounding_text = true;
    }
    profile.record_event_log = false;
    profile.text_reserve_bytes = keys * 3;
    return profile;
}

bool linearRssGrowth(const std::vector<benchmark::RssCheckpoint> &points)
{
    if (points.size() < 3 ||
        points.back().current_kib <= points.front().current_kib + 1024) {
        return false;
    }
    std::size_t increases = 0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        increases +=
            points[index].current_kib > points[index - 1].current_kib;
    }
    return increases * 5 >= (points.size() - 1) * 4;
}

template<typename Value>
bool linearGrowth(const std::vector<StabilityCheckpoint> &points,
                  Value value,
                  std::size_t material_growth)
{
    if (points.size() < 3 ||
        value(points.back()) <= value(points.front()) + material_growth) {
        return false;
    }
    std::size_t increases = 0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        increases += value(points[index]) > value(points[index - 1]);
    }
    return increases * 5 >= (points.size() - 1) * 4;
}

bool sustainedP99Growth(const std::vector<StabilityCheckpoint> &points,
                        double drift_percent)
{
    if (points.size() < 3 || drift_percent <= 25.0) {
        return false;
    }
    std::size_t increases = 0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        increases += points[index].p99_latency_ns >
                     points[index - 1].p99_latency_ns;
    }
    return increases * 5 >= (points.size() - 1) * 4;
}

core::TypingConvenienceOptions typingOptions(bool enabled)
{
    if (!enabled) {
        return {};
    }
    return {
        true,
        true,
        true,
        core::ShortcutScope::everywhere,
        core::ShortcutScope::everywhere,
    };
}

std::string referenceOutput(
    std::string_view name,
    std::size_t keys,
    const core::TypingConvenienceOptions &typing_options)
{
    IntegrationSession session{profileFor(name, keys), typing_options};
    CorpusCursor cursor;
    for (std::size_t index = 0; index < keys; ++index) {
        session.submit(cursor.next());
    }
    session.drain();
    return session.output();
}

IntegrationResult runProfile(
    std::string_view name,
    std::size_t keys,
    bool typing_conveniences)
{
    const core::TypingConvenienceOptions typing_options =
        typingOptions(typing_conveniences);
    const std::string expected =
        referenceOutput(name, keys, typing_options);

    IntegrationSession warmup{
        profileFor(name, 1000), typing_options};
    CorpusCursor warmup_cursor;
    for (std::size_t index = 0; index < std::min(keys, std::size_t{1000});
         ++index) {
        warmup.submit(warmup_cursor.next());
    }
    warmup.drain();

    // Materialize sample storage before the initial RSS reading so the
    // benchmark measures runtime state growth, not its fixed result buffer.
    std::vector<std::uint64_t> samples(keys);
    std::vector<double> checkpoint_means;
    checkpoint_means.reserve(10);

    IntegrationSession session{profileFor(name, keys), typing_options};
    IntegrationResult result;
    result.name = std::string(name) +
                  (typing_conveniences
                       ? "-typing-enabled"
                       : "-typing-disabled");
    result.total_keys = keys;
    result.rss.initial_kib = benchmark::currentRssKiB();
    result.rss.after_warmup_kib = result.rss.initial_kib;
    result.open_file_descriptors.initial =
        benchmark::openFileDescriptorCount();
    result.threads.initial = benchmark::threadCount();
    const double initial_cpu_seconds = benchmark::processCpuSeconds();

    CorpusCursor cursor;
    const std::size_t checkpoint_interval = std::max(keys / 10, std::size_t{1});
    std::uint64_t checkpoint_latency = 0;
    std::size_t checkpoint_sample_count = 0;
    const auto run_start = Clock::now();
    for (std::size_t index = 0; index < keys; ++index) {
        const auto start = Clock::now();
        session.submit(cursor.next());
        const auto end = Clock::now();
        const auto latency = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count());
        samples[index] = latency;
        checkpoint_latency += latency;
        ++checkpoint_sample_count;

        if ((index + 1) % checkpoint_interval == 0 || index + 1 == keys) {
            result.rss.checkpoints.push_back(
                {index + 1, benchmark::currentRssKiB()});
            result.stability_checkpoints.push_back({
                index + 1,
                result.rss.checkpoints.back().current_kib,
                benchmark::openFileDescriptorCount(),
                benchmark::threadCount(),
                0.0,
            });
            checkpoint_means.push_back(
                static_cast<double>(checkpoint_latency) /
                static_cast<double>(checkpoint_sample_count));
            checkpoint_latency = 0;
            checkpoint_sample_count = 0;
        }
    }
    session.drain();
    const auto run_end = Clock::now();

    result.total_seconds =
        std::chrono::duration<double>(run_end - run_start).count();
    result.keys_per_second =
        static_cast<double>(keys) / result.total_seconds;
    result.process_cpu_seconds =
        benchmark::processCpuSeconds() - initial_cpu_seconds;
    result.process_cpu_utilization_percent =
        result.total_seconds == 0.0
            ? 0.0
            : result.process_cpu_seconds / result.total_seconds * 100.0;
    result.latency = benchmark::calculateStatistics(samples);
    result.latency_drift_percent =
        benchmark::calculateStabilityDriftPercent(checkpoint_means);
    result.rss.final_kib = benchmark::currentRssKiB();
    result.rss.maximum_kib = benchmark::maximumRssKiB();
    result.rss.maximum_kib =
        std::max(result.rss.maximum_kib, result.rss.final_kib);
    result.rss.linear_growth_detected =
        linearRssGrowth(result.rss.checkpoints);
    result.open_file_descriptors.final =
        benchmark::openFileDescriptorCount();
    result.threads.final = benchmark::threadCount();
    result.open_file_descriptors.maximum =
        result.open_file_descriptors.initial;
    result.threads.maximum = result.threads.initial;

    std::vector<double> checkpoint_p99;
    checkpoint_p99.reserve(result.stability_checkpoints.size());
    std::vector<std::uint64_t> checkpoint_samples;
    checkpoint_samples.reserve(checkpoint_interval);
    for (std::size_t checkpoint = 0;
         checkpoint < result.stability_checkpoints.size();
         ++checkpoint) {
        const std::size_t begin = checkpoint * checkpoint_interval;
        const std::size_t end =
            std::min(begin + checkpoint_interval, samples.size());
        checkpoint_samples.assign(
            samples.begin() + static_cast<std::ptrdiff_t>(begin),
            samples.begin() + static_cast<std::ptrdiff_t>(end));
        const double p99 =
            benchmark::calculateStatistics(checkpoint_samples).p99_ns;
        StabilityCheckpoint &point =
            result.stability_checkpoints[checkpoint];
        point.p99_latency_ns = p99;
        checkpoint_p99.push_back(p99);
        result.open_file_descriptors.maximum = std::max(
            result.open_file_descriptors.maximum,
            point.open_file_descriptors);
        result.threads.maximum =
            std::max(result.threads.maximum, point.threads);
    }
    result.open_file_descriptors.maximum = std::max(
        result.open_file_descriptors.maximum,
        result.open_file_descriptors.final);
    result.threads.maximum =
        std::max(result.threads.maximum, result.threads.final);
    result.p99_latency_drift_percent =
        benchmark::calculateStabilityDriftPercent(checkpoint_p99);
    result.p99_latency_growth_detected =
        sustainedP99Growth(result.stability_checkpoints,
                           result.p99_latency_drift_percent);
    result.open_file_descriptors.linear_growth_detected = linearGrowth(
        result.stability_checkpoints,
        [](const StabilityCheckpoint &point) {
            return point.open_file_descriptors;
        },
        0);
    result.threads.linear_growth_detected = linearGrowth(
        result.stability_checkpoints,
        [](const StabilityCheckpoint &point) { return point.threads; },
        0);

    benchmark::Checksum checksum;
    checksum.add(session.output());
    result.checksum = checksum.value();

    const core::TransactionMetrics &metrics = session.metrics();
    result.max_queue_depth = metrics.max_queue_depth;
    result.final_queue_depth = metrics.queue_depth;
    result.completed_transactions = metrics.completed_transactions;
    result.aborted_transactions = metrics.aborted_transactions;
    result.stale_callbacks = metrics.stale_result_count;
    result.duplicate_preventions = metrics.duplicate_prevention_count;
    result.reset_count = metrics.reset_count;
    result.queue_overflow_count = metrics.queue_overflow_count;
    result.pending_transaction = metrics.active_transaction;

    const bool output_mismatch = session.output() != expected;
    const std::size_t applied_events = session.appliedEvents();
    result.lost_events =
        applied_events < keys ? keys - applied_events : 0;
    result.duplicate_events =
        applied_events > keys ? applied_events - keys : 0;
    result.reordered_events =
        output_mismatch && result.lost_events == 0 &&
                result.duplicate_events == 0
            ? 1
            : 0;
    result.errors += output_mismatch;
    result.errors += !core::isValidUtf8(session.output());
    result.errors += result.lost_events != 0;
    result.errors += result.duplicate_events != 0;
    result.errors += result.reordered_events != 0;
    result.errors += result.final_queue_depth != 0;
    result.errors += result.pending_transaction;
    result.errors += result.queue_overflow_count != 0;
    result.errors += result.stale_callbacks != 0;
    // Short sanitizer smoke runs can include lazy runtime page commitment.
    // Treat RSS slope as a soak invariant only; smoke still reports it.
    result.errors +=
        keys >= 1'000'000 && result.rss.linear_growth_detected;
    result.errors += keys >= 1'000'000 &&
                     result.p99_latency_growth_detected;
    result.errors += keys >= 1'000'000 &&
                     result.open_file_descriptors.linear_growth_detected;
    result.errors += keys >= 1'000'000 &&
                     result.threads.linear_growth_detected;
    if (name != "stale") {
        result.errors += result.aborted_transactions != 0;
    }
    return result;
}

} // namespace

IntegrationReport runBenchmarks(const BenchmarkOptions &options)
{
    IntegrationReport report;
    report.metadata = benchmark::collectReportMetadata();
    static constexpr std::string_view profiles[]{
        "immediate", "delayed", "stale"};
    for (const std::string_view profile : profiles) {
        if (options.profile == "all" || options.profile == profile) {
            report.results.push_back(runProfile(
                profile, options.keys, options.typing_conveniences));
        }
    }
    return report;
}

} // namespace unilume::integration_benchmark
