// SPDX-License-Identifier: GPL-2.0-or-later

#include "direct_commit_controller.h"

#include <algorithm>
#include <string>

namespace {

void eraseLastUtf8Character(std::string &text)
{
    std::size_t position = text.size() - 1;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    text.erase(position);
}

} // namespace

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
    finishActive(success, false);
}

void DirectCommitController::timeout(std::uint64_t sequence_id)
{
    if (!transaction_.active() ||
        sequence_id != transaction_.sequenceId()) {
        ++metrics_.stale_result_count;
        return;
    }
    const bool cancelled = backend_.cancel(sequence_id);
    if (!cancelled) {
        ++metrics_.stale_result_count;
        ++metrics_.uncertain_outcome_count;
        backend_.reset();
    }
    finishActive(false, cancelled);
}

void DirectCommitController::resetForFocus()
{
    if (transaction_.active()) {
        if (!backend_.cancel(transaction_.sequenceId())) {
            ++metrics_.uncertain_outcome_count;
        }
        transaction_.abort();
        ++metrics_.aborted_transactions;
        transaction_.clear();
    }
    backend_.reset();
    queue_head_ = 0;
    queue_size_ = 0;
    updateQueueMetrics();
    metrics_.active_transaction = false;
    engine_.reset();
    ++metrics_.reset_count;
}

void DirectCommitController::lineBreak()
{
    resetForFocus();
    engine_.lineBreak();
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

void DirectCommitController::setTypingOptions(
    const TypingConvenienceOptions &options)
{
    resetForFocus();
    engine_.setTypingOptions(options);
}

void DirectCommitController::setMacros(const macro::Snapshot &snapshot)
{
    resetForFocus();
    engine_.setMacros(snapshot);
}

void DirectCommitController::setKeymap(const keymap::Snapshot &snapshot)
{
    resetForFocus();
    engine_.setKeymap(snapshot);
}

void DirectCommitController::setDictionary(
    const dictionary::Snapshot &snapshot)
{
    resetForFocus();
    engine_.setDictionary(snapshot);
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
    if (result.delete_before_cursor == 0 &&
        input.kind == KeyKind::text &&
        result.commit_text == input.text) {
        return SubmissionStatus::passthrough;
    }
    return startTransaction(input, result);
}

SubmissionStatus DirectCommitController::startTransaction(
    const KeyInput &input,
    const KeyResult &result)
{
    if (result.require_fallback) {
        ++metrics_.aborted_transactions;
        ++metrics_.reset_count;
        engine_.reset();
        return SubmissionStatus::unhandled;
    }
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
        return SubmissionStatus::unhandled;
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
        ++metrics_.duplicate_prevention_count;
        (void)finishActive(false, false);
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
    queued.text_size = static_cast<std::uint8_t>(input.text.size());
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

bool DirectCommitController::finishActive(bool success,
                                          bool fallback_allowed)
{
    bool fallback_succeeded = false;
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
        if (fallback_allowed && !fallback_text.empty()) {
            fallback_succeeded =
                backend_.requestFallbackCommit(sequence, fallback_text) ==
                platform::ReplacementStatus::completed;
            if (!fallback_succeeded) {
                ++metrics_.fallback_failure_count;
            }
        }
    }
    transaction_.clear();
    metrics_.active_transaction = false;
    if (!draining_) {
        drainQueue();
    }
    return success || fallback_succeeded;
}

void DirectCommitController::drainQueue()
{
    draining_ = true;
    while (!transaction_.active() && queue_size_ != 0) {
        const std::size_t pending_inputs = queue_size_;
        std::string aggregate_commit;
        aggregate_commit.reserve(ReplacementTransaction::text_capacity);
        std::int32_t aggregate_delete = 0;
        std::uint64_t final_sequence = 0;
        bool valid = true;

        for (std::size_t index = 0; index < pending_inputs; ++index) {
            const QueuedInput queued = dequeue();
            const KeyResult result = engine_.process(view(queued));
            if (!result.handled || result.require_fallback ||
                result.delete_before_cursor < 0) {
                valid = false;
                break;
            }
            for (std::int32_t count = 0;
                 count < result.delete_before_cursor; ++count) {
                if (aggregate_commit.empty()) {
                    ++aggregate_delete;
                } else {
                    eraseLastUtf8Character(aggregate_commit);
                }
            }
            if (aggregate_commit.size() + result.commit_text.size() >
                ReplacementTransaction::text_capacity) {
                valid = false;
                break;
            }
            aggregate_commit.append(result.commit_text);
            final_sequence = result.sequence_id;
        }

        if (!valid || final_sequence == 0) {
            queue_head_ = 0;
            queue_size_ = 0;
            updateQueueMetrics();
            engine_.reset();
            ++metrics_.aborted_transactions;
            ++metrics_.reset_count;
            break;
        }

        const KeyResult aggregate{
            .handled = true,
            .sequence_id = final_sequence,
            .delete_before_cursor = aggregate_delete,
            .commit_text = aggregate_commit,
        };
        static_cast<void>(startTransaction(
            {KeyKind::reset, {}, false, false, false}, aggregate));
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
