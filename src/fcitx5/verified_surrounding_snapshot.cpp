// SPDX-License-Identifier: GPL-2.0-or-later

#include "verified_surrounding_snapshot.h"

#include "utf8_validation.h"

#include <fcitx/surroundingtext.h>

namespace unilume::fcitx5 {

bool SurroundingSnapshotValidation::allowsReplacement() const
{
    return capability && cursor_valid && utf8_valid &&
           within_resource_limit;
}

SurroundingSnapshotValidation validateSurroundingSnapshot(
    bool surrounding_capability,
    const fcitx::SurroundingText &surrounding,
    std::int32_t delete_before_cursor)
{
    SurroundingSnapshotValidation result{
        .bytes = surrounding.text().size(),
        .capability = surrounding_capability,
    };
    if (!surrounding_capability || delete_before_cursor < 0) {
        return result;
    }
    result.cursor_valid =
        surrounding.isValid() &&
        surrounding.anchor() == surrounding.cursor() &&
        surrounding.cursor() >=
            static_cast<std::size_t>(delete_before_cursor);
    result.within_resource_limit =
        result.bytes <= max_verified_surrounding_bytes;
    result.utf8_valid =
        result.within_resource_limit &&
        core::isValidUtf8(surrounding.text());
    return result;
}

const SurroundingSnapshotValidation &VerifiedSurroundingTicket::prepare(
    bool surrounding_capability,
    const fcitx::SurroundingText &surrounding)
{
    validation_ = validateSurroundingSnapshot(
        surrounding_capability, surrounding, 0);
    text_data_ = surrounding.text().data();
    cursor_ = surrounding.cursor();
    anchor_ = surrounding.anchor();
    native_valid_ = surrounding.isValid();
    ready_ = true;
    return validation_;
}

std::optional<SurroundingSnapshotValidation>
VerifiedSurroundingTicket::consume(
    bool surrounding_capability,
    const fcitx::SurroundingText &surrounding,
    std::int32_t delete_before_cursor)
{
    if (!ready_) {
        return std::nullopt;
    }
    ready_ = false;
    if (surrounding_capability != validation_.capability ||
        surrounding.text().data() != text_data_ ||
        surrounding.text().size() != validation_.bytes ||
        surrounding.cursor() != cursor_ ||
        surrounding.anchor() != anchor_ ||
        surrounding.isValid() != native_valid_) {
        return std::nullopt;
    }
    SurroundingSnapshotValidation result = validation_;
    result.cursor_valid =
        result.cursor_valid &&
        delete_before_cursor >= 0 &&
        cursor_ >= static_cast<std::size_t>(delete_before_cursor);
    return result;
}

void VerifiedSurroundingTicket::clear()
{
    ready_ = false;
}

} // namespace unilume::fcitx5
