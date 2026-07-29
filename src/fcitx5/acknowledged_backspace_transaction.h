// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace unilume::fcitx5 {

enum class BackspaceAcknowledgement {
    unexpected,
    forward_deletion,
};

enum class BackspaceReleaseAcknowledgement {
    unexpected,
    emit_next,
    complete,
};

class AcknowledgedBackspaceTransaction {
public:
    static constexpr std::size_t maximum_deletions = 128;

    bool prepare(std::uint64_t sequence_id,
                 std::size_t deletions,
                 std::string_view commit_text);
    [[nodiscard]] BackspaceAcknowledgement acknowledge();
    [[nodiscard]] BackspaceReleaseAcknowledgement acknowledgeRelease();
    [[nodiscard]] bool active() const;
    [[nodiscard]] std::uint64_t sequenceId() const;
    [[nodiscard]] std::string_view commitText() const;
    void clear();

private:
    std::uint64_t sequence_id_{};
    std::size_t remaining_deletions_{};
    std::string commit_text_;
    bool active_{};
    bool deletion_press_acknowledged_{};
};

} // namespace unilume::fcitx5
