// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "engine_context.h"
#include "replacement_backend.h"
#include "replacement_transaction.h"
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
    static constexpr std::size_t queue_capacity = 64;

    explicit DirectCommitController(
        platform::ReplacementBackend &backend,
        UlInputMethod method = UL_INPUT_METHOD_TELEX);

    SubmissionStatus submit(const KeyInput &input);
    void complete(std::uint64_t sequence_id, bool success);
    void timeout(std::uint64_t sequence_id);
    void resetForFocus();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);
    void setMacros(const macro::Snapshot &snapshot);
    void setKeymap(const keymap::Snapshot &snapshot);

    [[nodiscard]] const TransactionMetrics &metrics() const;
    [[nodiscard]] TransactionState transactionState() const;
    [[nodiscard]] std::uint64_t activeSequence() const;

private:
    static constexpr std::size_t queued_text_capacity = 32;

    struct QueuedInput {
        KeyKind kind{KeyKind::text};
        std::array<char, queued_text_capacity> text{};
        std::size_t text_size{};
        bool shift_pressed{};
        bool caps_lock_on{};
        bool has_control_modifier{};
    };

    SubmissionStatus processNow(const KeyInput &input);
    SubmissionStatus startTransaction(const KeyInput &input,
                                      const KeyResult &result);
    SubmissionStatus fallback(const KeyInput &input,
                              std::uint64_t sequence_id);
    bool enqueue(const KeyInput &input);
    QueuedInput dequeue();
    static KeyInput view(const QueuedInput &input);
    void finishActive(bool success);
    void drainQueue();
    void updateQueueMetrics();

    platform::ReplacementBackend &backend_;
    EngineContext engine_;
    ReplacementTransaction transaction_;
    TransactionMetrics metrics_;
    std::array<QueuedInput, queue_capacity> queue_{};
    std::size_t queue_head_{};
    std::size_t queue_size_{};
    bool draining_{};
};

} // namespace unilume::core
