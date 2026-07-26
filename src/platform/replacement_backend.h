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

class ReplacementBackend {
public:
    virtual ~ReplacementBackend() = default;

    // A verified replacement may delete text and must fail without a current,
    // trustworthy surrounding-text snapshot. "failed" guarantees that no
    // part of the request was applied; "pending" must eventually complete
    // exactly once unless cancel() confirms cancellation.
    [[nodiscard]] virtual bool canReplace(
        std::int32_t delete_before_cursor) const = 0;
    virtual ReplacementStatus requestReplacement(
        std::uint64_t sequence_id,
        std::int32_t delete_before_cursor,
        std::string_view commit_text) = 0;

    // Raw fallback never deletes surrounding text, is independent of the
    // direct-replacement capability, and must return a terminal status. It is
    // separate so a direct edit cannot accidentally bypass snapshot
    // validation.
    virtual ReplacementStatus requestFallbackCommit(
        std::uint64_t sequence_id,
        std::string_view commit_text) = 0;

    // true means a pending request is known not to have been applied.
    virtual bool cancel(std::uint64_t sequence_id) = 0;

    // Fence every request from the previous focus/frontend generation.
    virtual void reset() = 0;
};

} // namespace unilume::platform
