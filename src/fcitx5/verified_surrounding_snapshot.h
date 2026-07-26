// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

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

// A single-use ticket avoids validating the same immutable Fcitx snapshot
// twice in one synchronous key event. Metadata matching prevents reuse after
// Fcitx replaces the snapshot; consuming the ticket prevents cross-event
// reuse when storage happens to be recycled.
class VerifiedSurroundingTicket {
public:
    [[nodiscard]] const SurroundingSnapshotValidation &prepare(
        bool surrounding_capability,
        const fcitx::SurroundingText &surrounding);
    [[nodiscard]] std::optional<SurroundingSnapshotValidation> consume(
        bool surrounding_capability,
        const fcitx::SurroundingText &surrounding,
        std::int32_t delete_before_cursor);
    void clear();

private:
    SurroundingSnapshotValidation validation_;
    const char *text_data_{};
    std::size_t cursor_{};
    std::size_t anchor_{};
    bool native_valid_{};
    bool ready_{};
};

} // namespace unilume::fcitx5
