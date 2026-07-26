// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "direct_commit_controller.h"
#include "fcitx_replacement_backend.h"
#include "preedit_fallback_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace unilume::fcitx5 {

enum class TraceResetReason : std::uint8_t {
    none,
    focus,
    navigation,
    control_shortcut,
    capability_loss,
};

class DiagnosticTrace {
public:
    DiagnosticTrace();

    [[nodiscard]] std::uint64_t beginEvent() const;
    void recordDirect(
        core::SubmissionStatus status,
        const core::TransactionMetrics &metrics,
        const ReplacementObservation &replacement,
        std::uint64_t started_at_ns);
    void recordPreedit(
        const core::PreeditAction &action,
        bool surrounding_available,
        std::uint64_t started_at_ns);
    void recordReset(TraceResetReason reason);
    void recordModeChange(bool preedit);
    void flush() const;

private:
    struct Event {
        std::uint64_t sequence{};
        std::int32_t delete_before_cursor{};
        std::size_t commit_bytes{};
        std::size_t preedit_bytes{};
        std::size_t queue_depth{};
        std::uint64_t reset_count{};
        std::uint64_t stale_count{};
        std::uint64_t uncertain_count{};
        std::uint64_t fallback_failure_count{};
        std::uint64_t duration_ns{};
        std::uint8_t status{};
        TraceResetReason reset_reason{};
        bool preedit{};
        bool handled{};
        bool surrounding_available{};
        bool cursor_valid{};
    };

    void append(Event event);

    static constexpr std::size_t capacity = 64;
    std::array<Event, capacity> events_{};
    std::size_t next_{};
    std::size_t size_{};
    std::uint64_t total_events_{};
    std::uint64_t reset_events_{};
    std::uint64_t mode_changes_{};
    std::uint64_t context_id_{};
    bool enabled_{};
};

} // namespace unilume::fcitx5
