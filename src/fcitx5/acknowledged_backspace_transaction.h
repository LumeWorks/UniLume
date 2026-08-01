// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "direct_strategy.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace unilume::fcitx5 {

enum class BackspaceAcknowledgement {
    unexpected,
    forward_deletion,
    consume_sentinel_fast,
    consume_sentinel_guarded,
};

enum class BackspaceReleaseAcknowledgement {
    unexpected,
    forward_deletion,
    consume_sentinel,
    complete_guarded,
};

class AcknowledgedBackspaceTransaction {
public:
    static constexpr std::size_t maximum_deletions = 128;

    bool prepare(std::uint64_t sequence_id,
                 std::size_t deletions,
                 std::string_view commit_text,
                 DirectStrategy strategy);
    [[nodiscard]] BackspaceAcknowledgement acknowledge();
    [[nodiscard]] BackspaceReleaseAcknowledgement acknowledgeRelease();
    void markPressesDispatched(std::size_t count);
    [[nodiscard]] bool active() const;
    [[nodiscard]] std::uint64_t sequenceId() const;
    [[nodiscard]] std::size_t deletions() const;
    [[nodiscard]] std::size_t pressesToDispatch() const;
    [[nodiscard]] std::string_view commitText() const;
    [[nodiscard]] std::size_t outstandingPresses() const;
    [[nodiscard]] bool releasePending() const;
    void clear();

private:
    std::uint64_t sequence_id_{};
    std::size_t deletions_{};
    std::size_t acknowledged_presses_{};
    std::size_t dispatched_presses_{};
    std::string commit_text_;
    DirectStrategy strategy_{DirectStrategy::fast};
    bool active_{};
    bool press_acknowledged_{};
    bool sentinel_press_acknowledged_{};
};

} // namespace unilume::fcitx5
