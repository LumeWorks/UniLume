// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "direct_commit_controller.h"
#include "fcitx_replacement_backend.h"
#include "preedit_fallback_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace unilume::fcitx5 {

enum class TraceResetReason : std::uint8_t {
    none,
    focus,
    navigation,
    control_shortcut,
    capability_loss,
};

enum class TracePath : std::uint8_t {
    direct,
    preedit,
    off,
};

enum class TraceCapability : std::uint8_t {
    unavailable,
    non_atomic_transport,
    ready,
    invalid_cursor,
    invalid_utf8,
    resource_limit,
    count,
};

enum class TraceError : std::uint8_t {
    none,
    fallback,
    stale_result,
    uncertain_outcome,
    backend_failure,
    capability_loss,
};

enum class TraceDurationBucket : std::uint8_t {
    under_1_us,
    under_5_us,
    under_20_us,
    under_100_us,
    under_1_ms,
    at_least_1_ms,
    count,
};

struct DiagnosticSettings {
    bool enabled{};
    std::filesystem::path export_path;
};

struct DiagnosticSnapshot {
    std::uint64_t total_events{};
    std::uint64_t retained_events{};
    std::uint64_t reset_events{};
    std::uint64_t mode_changes{};
    std::uint64_t preedit_handoffs{};
    std::uint64_t fallback_events{};
    std::uint64_t stale_events{};
    std::uint64_t uncertain_events{};
    std::uint64_t backend_failures{};
    std::uint64_t capability_losses{};
    std::array<std::uint64_t,
               static_cast<std::size_t>(TraceCapability::count)>
        capabilities{};
    std::array<std::uint64_t,
               static_cast<std::size_t>(TraceDurationBucket::count)>
        durations{};
};

class DiagnosticTrace {
public:
    enum class EventKind : std::uint8_t {
        key,
        reset,
        mode_change,
        preedit_handoff,
    };

    DiagnosticTrace();
    explicit DiagnosticTrace(DiagnosticSettings settings);
    DiagnosticTrace(const DiagnosticTrace &) = delete;
    DiagnosticTrace &operator=(const DiagnosticTrace &) = delete;
    DiagnosticTrace(DiagnosticTrace &&) = delete;
    DiagnosticTrace &operator=(DiagnosticTrace &&) = delete;

    [[nodiscard]] std::uint64_t beginEvent() const;
    void recordDirect(
        core::SubmissionStatus status,
        const core::TransactionMetrics &metrics,
        const ReplacementObservation &replacement,
        std::uint64_t started_at_ns);
    void recordPreedit(
        const core::PreeditAction &action,
        const ReplacementObservation &replacement,
        std::uint64_t started_at_ns);
    void recordReset(TraceResetReason reason);
    void recordModeChange(
        bool preedit,
        const ReplacementObservation &replacement);
    void recordPreeditHandoff(
        const ReplacementObservation &replacement);

    // Rendering and file export are explicit cold-path operations. Event
    // methods accept no text, clipboard, application identity or surrounding
    // content, so those values cannot enter the retained ring.
    [[nodiscard]] DiagnosticSnapshot snapshot() const;
    [[nodiscard]] std::string renderBundle() const;
    [[nodiscard]] bool exportBundle(std::string *error = nullptr) const;
    void flush() const;

private:
    struct Event {
        std::uint64_t sequence{};
        std::uint64_t backend_sequence{};
        std::size_t queue_depth{};
        std::uint64_t reset_count{};
        std::uint64_t stale_count{};
        std::uint64_t uncertain_count{};
        std::uint64_t fallback_failure_count{};
        EventKind kind{};
        TracePath path{};
        TraceCapability capability{};
        TraceError error{};
        TraceDurationBucket duration{};
        TraceResetReason reset_reason{};
        std::uint8_t status{};
        bool handled{};
    };

    [[nodiscard]] TraceError directError(
        core::SubmissionStatus status,
        const core::TransactionMetrics &metrics) const;
    static TraceCapability capability(
        const ReplacementObservation &replacement);
    static TraceDurationBucket durationBucket(std::uint64_t duration_ns);
    void append(Event event);

    static constexpr std::size_t capacity = 64;
    static constexpr std::size_t maximum_bundle_bytes = 64 * 1024;
    std::array<Event, capacity> events_{};
    std::array<std::uint64_t,
               static_cast<std::size_t>(TraceDurationBucket::count)>
        durations_{};
    std::filesystem::path export_path_;
    std::size_t next_{};
    std::size_t size_{};
    std::uint64_t total_events_{};
    std::uint64_t reset_events_{};
    std::uint64_t mode_changes_{};
    std::uint64_t preedit_handoffs_{};
    std::uint64_t fallback_events_{};
    std::uint64_t stale_events_{};
    std::uint64_t uncertain_events_{};
    std::uint64_t backend_failures_{};
    std::uint64_t capability_losses_{};
    std::array<std::uint64_t,
               static_cast<std::size_t>(TraceCapability::count)>
        capabilities_{};
    std::uint64_t context_id_{};
    core::TransactionMetrics previous_metrics_;
    bool enabled_{};
};

} // namespace unilume::fcitx5
