// SPDX-License-Identifier: GPL-2.0-or-later

#include "fcitx_replacement_backend.h"

#include "utf8_validation.h"
#include "verified_surrounding_snapshot.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx/surroundingtext.h>
#include <string>

namespace unilume::fcitx5 {

FcitxReplacementBackend::FcitxReplacementBackend(
    fcitx::InputContext &input_context)
    : input_context_(input_context)
{
}

bool FcitxReplacementBackend::supportsDirectReplacement() const
{
    observation_.delete_before_cursor = 0;
    observation_.generation = generation_;
    observation_.surrounding_available =
        input_context_.capabilityFlags().test(
            fcitx::CapabilityFlag::SurroundingText);
    if (!observation_.surrounding_available) {
        observation_.surrounding_bytes = 0;
        observation_.cursor_valid = false;
        observation_.within_resource_limit = false;
        observation_.utf8_valid = false;
        verified_ticket_.clear();
        return false;
    }
    const SurroundingSnapshotValidation &validated =
        verified_ticket_.prepare(
            observation_.surrounding_available,
            input_context_.surroundingText());
    observation_.surrounding_bytes = validated.bytes;
    observation_.cursor_valid = validated.cursor_valid;
    observation_.within_resource_limit =
        validated.within_resource_limit;
    observation_.utf8_valid = validated.utf8_valid;
    return validated.allowsReplacement();
}

bool FcitxReplacementBackend::canReplace(
    std::int32_t delete_before_cursor) const
{
    observation_.delete_before_cursor = delete_before_cursor;
    observation_.generation = generation_;
    observation_.surrounding_available =
        input_context_.capabilityFlags().test(
            fcitx::CapabilityFlag::SurroundingText);
    observation_.cursor_valid = false;
    observation_.utf8_valid = false;
    observation_.within_resource_limit = false;
    if (delete_before_cursor < 0 ||
        !observation_.surrounding_available) {
        verified_ticket_.clear();
        return false;
    }
    const fcitx::SurroundingText &surrounding =
        input_context_.surroundingText();
    const auto prepared = verified_ticket_.consume(
        observation_.surrounding_available,
        surrounding,
        delete_before_cursor);
    const SurroundingSnapshotValidation validated = prepared
        ? *prepared
        : validateSurroundingSnapshot(
              observation_.surrounding_available,
              surrounding,
              delete_before_cursor);
    observation_.surrounding_bytes = validated.bytes;
    observation_.cursor_valid = validated.cursor_valid;
    observation_.within_resource_limit =
        validated.within_resource_limit;
    observation_.utf8_valid = validated.utf8_valid;
    return validated.allowsReplacement();
}

platform::ReplacementStatus
FcitxReplacementBackend::requestReplacement(
    std::uint64_t sequence_id,
    std::int32_t delete_before_cursor,
    std::string_view commit_text)
{
    observation_.sequence_id = sequence_id;
    observation_.commit_bytes = commit_text.size();
    if (sequence_id <= last_sequence_id_ ||
        delete_before_cursor < 0 ||
        !core::isValidUtf8(commit_text)) {
        verified_ticket_.clear();
        return platform::ReplacementStatus::failed;
    }
    if (!canReplace(delete_before_cursor)) {
        return platform::ReplacementStatus::failed;
    }

    const auto delete_count =
        static_cast<std::size_t>(delete_before_cursor);
    if (delete_count != 0) {
        input_context_.deleteSurroundingText(
            -delete_before_cursor,
            static_cast<unsigned int>(delete_count));
    }
    if (!commit_text.empty()) {
        input_context_.commitString(std::string(commit_text));
    }
    last_sequence_id_ = sequence_id;
    return platform::ReplacementStatus::completed;
}

platform::ReplacementStatus
FcitxReplacementBackend::requestFallbackCommit(
    std::uint64_t sequence_id,
    std::string_view commit_text)
{
    observation_.sequence_id = sequence_id;
    observation_.delete_before_cursor = 0;
    observation_.commit_bytes = commit_text.size();
    observation_.generation = generation_;
    verified_ticket_.clear();
    if (sequence_id <= last_sequence_id_ ||
        !core::isValidUtf8(commit_text)) {
        return platform::ReplacementStatus::failed;
    }
    if (!commit_text.empty()) {
        input_context_.commitString(std::string(commit_text));
    }
    last_sequence_id_ = sequence_id;
    return platform::ReplacementStatus::completed;
}

bool FcitxReplacementBackend::cancel(std::uint64_t)
{
    // Fcitx commit/delete calls are synchronous requests on the event thread.
    return false;
}

void FcitxReplacementBackend::reset()
{
    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    observation_ = {};
    observation_.generation = generation_;
    verified_ticket_.clear();
}

const ReplacementObservation &
FcitxReplacementBackend::lastObservation() const
{
    return observation_;
}

} // namespace unilume::fcitx5
