// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace unilume::platform {

enum class ReplacementStatus {
    completed,
    pending,
    failed,
};

// Terminal classification of an asynchronous replacement completion.
//
//   applied     - the complete delete-and-insert replacement is known to have
//                 been applied. Only a backend operation with a real atomic
//                 guarantee (or an independent authoritative verification of
//                 the visible text) may report this. A synthetic event ACK is
//                 not sufficient proof by itself.
//
//   not_applied - no part of the replacement was dispatched or applied. Only
//                 valid when zero mutation is guaranteed, e.g. a validation or
//                 dispatch failure, or a confirmed cancellation before the
//                 first synthetic event.
//
//   uncertain   - some part may have been applied or application-visible state
//                 cannot be verified. Any synthetic event already dispatched,
//                 an unverifiable ACK, unknown ordering or an unknown partial
//                 mutation must be reported as uncertain. The outcome must
//                 fence the transaction, contain the queue and report exactly
//                 one uncertainty metric.
enum class ReplacementOutcome {
    applied,
    not_applied,
    uncertain,
};

class ReplacementBackend {
public:
    virtual ~ReplacementBackend() = default;

    // A verified replacement may require text delete and must fail without a
    // current, trustworthy surrounding-text snapshot. "failed" guarantees
    // that no part of the request was applied; "pending" must eventually
    // complete exactly once unless cancel() confirms cancellation.
    [[nodiscard]] virtual bool canReplace(
        std::int32_t delete_before_cursor) const = 0;
    virtual ReplacementStatus requestReplacement(
        std::uint64_t sequence_id,
        std::int32_t delete_before_cursor,
        std::string_view commit_text) = 0;

    // Raw fallback which never deletes surrounding text, is independent of
    // the direct-replacement capability, and must return a terminal status.
    // It is separate so a direct edit cannot accidentally bypass snapshot
    // validation.
    virtual ReplacementStatus requestFallbackCommit(
        std::uint64_t sequence_id,
        std::string_view commit_text) = 0;

    // true means a pending request is known not to have been applied.
    virtual bool cancel(std::uint64_t sequence_id) = 0;

    // The backend is the single owner of the poisoned/direct-disabled state
    // and the focus/frontend generation. Every fence and every uncertainty
    // containment must flow through this call so the controller never
    // introduces a second source of truth.
    virtual void markUncertainOutcome() = 0;

    // Fence every request from the previous focus/frontend generation.
    virtual void reset() = 0;
};

} // namespace unilume::platform
