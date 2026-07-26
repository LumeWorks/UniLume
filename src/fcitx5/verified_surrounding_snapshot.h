// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace fcitx {
class SurroundingText;
}

namespace unilume::fcitx5 {

inline constexpr std::size_t max_verified_surrounding_bytes = 64 * 1024;

struct SurroundingSnapshotValidation {
    std::size_t bytes{};
    bool capability{};
    bool cursor_valid{};
    bool utf8_valid{};
    bool within_resource_limit{};

    [[nodiscard]] bool allowsReplacement() const;
};

[[nodiscard]] SurroundingSnapshotValidation validateSurroundingSnapshot(
    bool surrounding_capability,
    const fcitx::SurroundingText &surrounding,
    std::int32_t delete_before_cursor);

} // namespace unilume::fcitx5
