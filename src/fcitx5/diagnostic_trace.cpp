// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostic_trace.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fcitx-utils/log.h>

namespace unilume::fcitx5 {
namespace {

std::atomic<std::uint64_t> next_context_id{1};

std::uint64_t monotonicNanoseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

DiagnosticTrace::DiagnosticTrace()
{
    const char *value = std::getenv("UNILUME_FCITX_DIAGNOSTICS");
    enabled_ =
        value != nullptr && value[0] == '1' && value[1] == '\0';
    if (enabled_) {
        context_id_ = next_context_id.fetch_add(
            1, std::memory_order_relaxed);
    }
}

std::uint64_t DiagnosticTrace::beginEvent() const
{
    return enabled_ ? monotonicNanoseconds() : 0;
}

void DiagnosticTrace::recordDirect(
    core::SubmissionStatus status,
    const core::TransactionMetrics &metrics,
    const ReplacementObservation &replacement,
    std::uint64_t started_at_ns)
{
    if (started_at_ns == 0) {
        return;
    }
    append({
        0,
        replacement.delete_before_cursor,
        replacement.commit_bytes,
        0,
        metrics.queue_depth,
        metrics.reset_count,
        metrics.stale_result_count,
        metrics.uncertain_outcome_count,
        metrics.fallback_failure_count,
        monotonicNanoseconds() - started_at_ns,
        static_cast<std::uint8_t>(status),
        TraceResetReason::none,
        false,
        status != core::SubmissionStatus::unhandled,
        replacement.surrounding_available,
        replacement.cursor_valid,
    });
}

void DiagnosticTrace::recordPreedit(
    const core::PreeditAction &action,
    bool surrounding_available,
    std::uint64_t started_at_ns)
{
    if (started_at_ns == 0) {
        return;
    }
    append({
        0,
        0,
        action.commit_text.size(),
        action.preedit_text.size(),
        0,
        reset_events_,
        0,
        0,
        0,
        monotonicNanoseconds() - started_at_ns,
        0,
        TraceResetReason::none,
        true,
        action.handled,
        surrounding_available,
        true,
    });
}

void DiagnosticTrace::recordReset(TraceResetReason reason)
{
    if (enabled_) {
        ++reset_events_;
        append({
            0,
            0,
            0,
            0,
            0,
            0,
            reset_events_,
            0,
            0,
            0,
            0,
            reason,
            false,
            false,
            false,
            false,
        });
    }
}

void DiagnosticTrace::recordModeChange(bool)
{
    if (enabled_) {
        ++mode_changes_;
    }
}

void DiagnosticTrace::flush() const
{
    if (!enabled_ ||
        (total_events_ == 0 && reset_events_ == 0 &&
         mode_changes_ == 0)) {
        return;
    }
    FCITX_INFO()
        << "UniLume diagnostic summary"
        << " total_events=" << total_events_
        << " retained_events=" << size_
        << " resets=" << reset_events_
        << " mode_changes=" << mode_changes_
        << " context=" << context_id_;
    const std::size_t first =
        size_ == capacity ? next_ : 0;
    for (std::size_t offset = 0; offset < size_; ++offset) {
        const Event &event =
            events_[(first + offset) % capacity];
        FCITX_INFO()
            << "UniLume diagnostic event"
            << " sequence=" << event.sequence
            << " preedit=" << event.preedit
            << " handled=" << event.handled
            << " status=" << static_cast<unsigned int>(event.status)
            << " reset_reason="
            << static_cast<unsigned int>(event.reset_reason)
            << " delete=" << event.delete_before_cursor
            << " commit_bytes=" << event.commit_bytes
            << " preedit_bytes=" << event.preedit_bytes
            << " queue=" << event.queue_depth
            << " resets=" << event.reset_count
            << " stale=" << event.stale_count
            << " uncertain=" << event.uncertain_count
            << " fallback_failures=" << event.fallback_failure_count
            << " duration_ns=" << event.duration_ns
            << " surrounding=" << event.surrounding_available
            << " cursor_valid=" << event.cursor_valid;
    }
}

void DiagnosticTrace::append(Event event)
{
    if (!enabled_) {
        return;
    }
    event.sequence = ++total_events_;
    events_[next_] = event;
    next_ = (next_ + 1) % capacity;
    if (size_ < capacity) {
        ++size_;
    }
}

} // namespace unilume::fcitx5
