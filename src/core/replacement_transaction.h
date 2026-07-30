// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace unilume::core {

enum class TransactionState {
    idle,
    prepared,
    commit_requested,
    completed,
    aborted,
};

class ReplacementTransaction {
public:
    static constexpr std::size_t text_capacity = 2048;

    bool prepare(std::uint64_t sequence_id,
                 std::int32_t delete_before_cursor,
                 std::string_view commit_text,
                 std::string_view fallback_text);
    void markRequested();
    void complete();
    void abort();
    void clear();

    [[nodiscard]] TransactionState state() const;
    [[nodiscard]] bool active() const;
    [[nodiscard]] std::uint64_t sequenceId() const;
    [[nodiscard]] std::int32_t deleteBeforeCursor() const;
    [[nodiscard]] std::string_view commitText() const;
    [[nodiscard]] std::string_view fallbackText() const;

private:
    std::array<char, text_capacity> commit_buffer_{};
    std::array<char, text_capacity> fallback_buffer_{};
    std::size_t commit_size_{};
    std::size_t fallback_size_{};
    std::uint64_t sequence_id_{};
    std::int32_t delete_before_cursor_{};
    TransactionState state_{TransactionState::idle};
};

} // namespace unilume::core
