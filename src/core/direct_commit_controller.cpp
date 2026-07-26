// SPDX-License-Identifier: GPL-2.0-or-later

#include "direct_commit_controller.h"

#include <algorithm>

namespace unilume::core {

DirectCommitController::DirectCommitController(
    platform::ReplacementBackend &backend,
    UlInputMethod method)
    : backend_(backend), engine_(method)
{
}

SubmissionStatus DirectCommitController::submit(const KeyInput &input)
{
    if (input.kind == KeyKind::reset || input.kind == KeyKind::navigation ||
        input.has_control_modifier) {
        resetForFocus();
        return SubmissionStatus::unhandled;
    }
    if (transaction_.active()) {
        if (enqueue(input)) {
            return SubmissionStatus::queued;
        }
        ++metrics_.queue_overflow_count;
        timeout(transaction_.sequenceId());
        return processNow(input);
    }
    return processNow(input);
}

void DirectCommitController::complete(std::uint64_t sequence_id, bool success)
{
    if (!transaction_.active() ||
        sequence_id != transaction_.sequenceId()) {
        ++metrics_.stale_result_count;
        ++metrics_.duplicate_prevention_count;
        return;
    }
    finishActive(success);
}

void DirectCommitController::timeout(std::uint64_t sequence_id)
{
    if (!transaction_.active() ||
        sequence_id != transaction_.sequenceId()) {
        ++metrics_.stale_result_count;
        return;
    }
    if (!backend_.cancel(sequence_id)) {
        ++metrics_.stale_result_count;
    }
    finishActive(false);
}

void DirectCommitController::resetForFocus()
{
    if (transaction_.active()) {
        backend_.cancel(transaction_.sequenceId());
        transaction_.abort();
        ++metrics_.aborted_transactions;
        transaction_.clear();
    }
    queue_head_ = 0;
    queue_size_ = 0;
    updateQueueMetrics();
    metrics_.active_transaction = false;
    engine_.reset();
    ++metrics_.reset_count;
}

void DirectCommitController::setInputMethod(UlInputMethod method)
{
    resetForFocus();
    engine_.setInputMethod(method);
}

void DirectCommitController::setOptions(const UlEngineOptions &options)
{
    resetForFocus();
    engine_.setOptions(options);
}

void DirectCommitController::setMacros(const macro::Snapshot &snapshot)
{
    resetForFocus();
    engine_.setMacros(snapshot);
}

const TransactionMetrics &DirectCommitController::metrics() const
{
    return metrics_;
}

TransactionState DirectCommitController::transactionState() const
{
    return transaction_.state();
}

std::uint64_t DirectCommitController::activeSequence() const
{
    return transaction_.active() ? transaction_.sequenceId() : 0;
}

SubmissionStatus DirectCommitController::processNow(const KeyInput &input)
{
    const KeyResult result = engine_.process(input);
    if (!result.handled) {
        return SubmissionStatus::unhandled;
    }
    return startTransaction(input, result);
}

SubmissionStatus DirectCommitController::startTransaction(
    const KeyInput &input,
    const KeyResult &result)
{
    const std::string_view fallback_text =
        input.kind == KeyKind::text ? input.text : std::string_view{};
    if (!transaction_.prepare(
            result.sequence_id,
            result.delete_before_cursor,
            result.commit_text,
            fallback_text)) {
        engine_.reset();
        ++metrics_.aborted_transactions;
        ++metrics_.reset_count;
        return SubmissionStatus::fallback;
    }

    if (!backend_.canReplace(result.delete_before_cursor)) {
        transaction_.abort();
        ++metrics_.aborted_transactions;
        ++metrics_.duplicate_prevention_count;
        const std::uint64_t sequence = transaction_.sequenceId();
        transaction_.clear();
        engine_.reset();
        ++metrics_.reset_count;
        return fallback(input, sequence);
    }

    transaction_.markRequested();
    metrics_.active_transaction = true;
    const platform::ReplacementStatus status = backend_.requestReplacement(
        transaction_.sequenceId(),
        transaction_.deleteBeforeCursor(),
        transaction_.commitText());
    switch (status) {
    case platform::ReplacementStatus::completed:
        finishActive(true);
        return SubmissionStatus::handled;
    case platform::ReplacementStatus::pending:
        return SubmissionStatus::handled;
    case platform::ReplacementStatus::failed:
        finishActive(false);
        return SubmissionStatus::fallback;
    }
    return SubmissionStatus::fallback;
}

SubmissionStatus DirectCommitController::fallback(
    const KeyInput &input,
    std::uint64_t sequence_id)
{
    if (input.kind != KeyKind::text || input.text.empty()) {
        return SubmissionStatus::unhandled;
    }
    if (backend_.requestReplacement(sequence_id, 0, input.text) ==
        platform::ReplacementStatus::failed) {
        return SubmissionStatus::unhandled;
    }
    return SubmissionStatus::fallback;
}

bool DirectCommitController::enqueue(const KeyInput &input)
{
    if (queue_size_ == queue_.size() ||
        input.text.size() > queued_text_capacity) {
        return false;
    }
    const std::size_t index = (queue_head_ + queue_size_) % queue_.size();
    QueuedInput &queued = queue_[index];
    queued.kind = input.kind;
    queued.text_size = input.text.size();
    std::copy(input.text.begin(), input.text.end(), queued.text.begin());
    queued.shift_pressed = input.shift_pressed;
    queued.caps_lock_on = input.caps_lock_on;
    queued.has_control_modifier = input.has_control_modifier;
    ++queue_size_;
    updateQueueMetrics();
    return true;
}

DirectCommitController::QueuedInput DirectCommitController::dequeue()
{
    const QueuedInput queued = queue_[queue_head_];
    queue_head_ = (queue_head_ + 1) % queue_.size();
    --queue_size_;
    updateQueueMetrics();
    return queued;
}

KeyInput DirectCommitController::view(const QueuedInput &input)
{
    return {
        input.kind,
        std::string_view{input.text.data(), input.text_size},
        input.shift_pressed,
        input.caps_lock_on,
        input.has_control_modifier,
    };
}

void DirectCommitController::finishActive(bool success)
{
    if (success) {
        transaction_.complete();
        ++metrics_.completed_transactions;
    } else {
        const std::uint64_t sequence = transaction_.sequenceId();
        const std::string_view fallback_text = transaction_.fallbackText();
        transaction_.abort();
        ++metrics_.aborted_transactions;
        engine_.reset();
        ++metrics_.reset_count;
        if (!fallback_text.empty()) {
            backend_.requestReplacement(sequence, 0, fallback_text);
        }
    }
    transaction_.clear();
    metrics_.active_transaction = false;
    if (!draining_) {
        drainQueue();
    }
}

void DirectCommitController::drainQueue()
{
    draining_ = true;
    while (!transaction_.active() && queue_size_ != 0) {
        const QueuedInput queued = dequeue();
        processNow(view(queued));
    }
    draining_ = false;
}

void DirectCommitController::updateQueueMetrics()
{
    metrics_.queue_depth = queue_size_;
    metrics_.max_queue_depth =
        std::max(metrics_.max_queue_depth, queue_size_);
}

} // namespace unilume::core
