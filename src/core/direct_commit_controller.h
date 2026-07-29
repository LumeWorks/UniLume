// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "replacement_backend.h"
#include "replacement_transaction.h"
#include "typing_pipeline.h"
#include "transaction_metrics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace unilume::core {

enum class SubmissionStatus {
    handled,
    unhandled,
    queued,
    fallback,
};

class DirectCommitController {
public:
    // A zero-preedit backend may need several frontend round trips for one
    // replacement. Keep a fixed, allocation-free burst window large enough
    // for paste-like keyboard injection without making the queue unbounded.
    static constexpr std::size_t queue_capacity = 512;

    explicit DirectCommitController(
        platform::ReplacementBackend &backend,
        UlInputMethod method = UL_INPUT_METHOD_TELEX);

    SubmissionStatus submit(const KeyInput &input);
    void complete(std::uint64_t sequence_id, bool success);
    void timeout(std::uint64_t sequence_id);
    void resetForFocus();
    void lineBreak();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);
    void setTypingOptions(const TypingConvenienceOptions &options);
    void setMacros(const macro::Snapshot &snapshot);
    void setKeymap(const keymap::Snapshot &snapshot);
    void setDictionary(const dictionary::Snapshot &snapshot);

    [[nodiscard]] const TransactionMetrics &metrics() const;
    [[nodiscard]] TransactionState transactionState() const;
    [[nodiscard]] std::uint64_t activeSequence() const;

private:
    static constexpr std::size_t queued_text_capacity = 32;

    struct QueuedInput {
        KeyKind kind{KeyKind::text};
        std::array<char, queued_text_capacity> text{};
        std::uint8_t text_size{};
        bool shift_pressed{};
        bool caps_lock_on{};
        bool has_control_modifier{};
    };

    static_assert(sizeof(QueuedInput) <= 40);

    SubmissionStatus processNow(const KeyInput &input);
    SubmissionStatus startTransaction(const KeyInput &input,
                                      const KeyResult &result);
    SubmissionStatus fallback(const KeyInput &input,
                              std::uint64_t sequence_id);
    bool enqueue(const KeyInput &input);
    QueuedInput dequeue();
    static KeyInput view(const QueuedInput &input);
    bool finishActive(bool success, bool fallback_allowed = true);
    void drainQueue();
    void updateQueueMetrics();

    platform::ReplacementBackend &backend_;
    TypingPipeline engine_;
    ReplacementTransaction transaction_;
    TransactionMetrics metrics_;
    std::array<QueuedInput, queue_capacity> queue_{};
    std::size_t queue_head_{};
    std::size_t queue_size_{};
    bool draining_{};
};

} // namespace unilume::core
