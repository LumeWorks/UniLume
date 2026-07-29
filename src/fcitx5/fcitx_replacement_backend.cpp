// SPDX-License-Identifier: GPL-2.0-or-later

#include "fcitx_replacement_backend.h"

#include "replacement_transport_contract.h"
#include "utf8_validation.h"
#include "verified_surrounding_snapshot.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx/surroundingtext.h>
#include <string>

namespace unilume::fcitx5 {

FcitxReplacementBackend::FcitxReplacementBackend(
    fcitx::InputContext &input_context,
    UinputBackspaceDevice &uinput_device)
    : input_context_(input_context),
      uinput_device_(uinput_device)
{
}

bool FcitxReplacementBackend::supportsDirectReplacement() const
{
    observation_.delete_before_cursor = 0;
    observation_.generation = generation_;
    observation_.surrounding_available =
        input_context_.capabilityFlags().test(
            fcitx::CapabilityFlag::SurroundingText);
    observation_.atomic_transport = replacementTransportIsAtomic(
        input_context_.frontendName());
    observation_.acknowledged_uinput =
        !observation_.atomic_transport && uinput_device_.available();
    if (observation_.acknowledged_uinput) {
        observation_.surrounding_bytes = 0;
        observation_.cursor_valid = true;
        observation_.within_resource_limit = true;
        observation_.utf8_valid = true;
        verified_ticket_.clear();
        return true;
    }
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
    return observation_.atomic_transport &&
           validated.allowsReplacement();
}

bool FcitxReplacementBackend::canReplace(
    std::int32_t delete_before_cursor) const
{
    observation_.delete_before_cursor = delete_before_cursor;
    observation_.generation = generation_;
    observation_.surrounding_available =
        input_context_.capabilityFlags().test(
            fcitx::CapabilityFlag::SurroundingText);
    observation_.atomic_transport = replacementTransportIsAtomic(
        input_context_.frontendName());
    observation_.acknowledged_uinput =
        !observation_.atomic_transport && uinput_device_.available();
    observation_.cursor_valid = false;
    observation_.utf8_valid = false;
    observation_.within_resource_limit = false;
    if (delete_before_cursor < 0 ||
        (!observation_.acknowledged_uinput &&
         !observation_.surrounding_available)) {
        verified_ticket_.clear();
        return false;
    }
    if (observation_.acknowledged_uinput) {
        observation_.surrounding_bytes = 0;
        observation_.cursor_valid = true;
        observation_.within_resource_limit =
            delete_before_cursor <= static_cast<std::int32_t>(
                AcknowledgedBackspaceTransaction::maximum_deletions);
        observation_.utf8_valid = true;
        verified_ticket_.clear();
        return observation_.within_resource_limit;
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
    return observation_.atomic_transport &&
           validated.allowsReplacement();
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

    const auto delete_count = static_cast<std::size_t>(delete_before_cursor);
    if (observation_.acknowledged_uinput && delete_count != 0) {
        if (!acknowledged_transaction_.prepare(
                sequence_id, delete_count, commit_text)) {
            acknowledged_transaction_.clear();
            return platform::ReplacementStatus::failed;
        }
        initial_backspace_pending_ = true;
        return platform::ReplacementStatus::pending;
    }
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

bool FcitxReplacementBackend::cancel(std::uint64_t sequence_id)
{
    if (acknowledged_transaction_.active() &&
        acknowledged_transaction_.sequenceId() == sequence_id) {
        const bool known_unapplied = initial_backspace_pending_;
        acknowledged_transaction_.clear();
        initial_backspace_pending_ = false;
        forwarded_backspace_release_pending_ = false;
        barrier_release_pending_ = false;
        return known_unapplied;
    }
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
    acknowledged_transaction_.clear();
    initial_backspace_pending_ = false;
    forwarded_backspace_release_pending_ = false;
    barrier_release_pending_ = false;
}

const ReplacementObservation &
FcitxReplacementBackend::lastObservation() const
{
    return observation_;
}

bool FcitxReplacementBackend::acknowledgedDeletionPending() const
{
    return acknowledged_transaction_.active();
}

bool FcitxReplacementBackend::initialBackspacePending() const
{
    return initial_backspace_pending_;
}

bool FcitxReplacementBackend::startAcknowledgedReplacement()
{
    if (!initial_backspace_pending_ ||
        !acknowledged_transaction_.active()) {
        return false;
    }
    initial_backspace_pending_ = false;
    if (uinput_device_.emitBackspace()) {
        return true;
    }
    acknowledged_transaction_.clear();
    return false;
}

BackspaceAcknowledgement FcitxReplacementBackend::acknowledgeBackspace()
{
    return acknowledged_transaction_.acknowledge();
}

void FcitxReplacementBackend::expectForwardedBackspaceRelease()
{
    forwarded_backspace_release_pending_ = true;
}

bool FcitxReplacementBackend::forwardedBackspaceReleasePending() const
{
    return forwarded_backspace_release_pending_;
}

bool FcitxReplacementBackend::continueAcknowledgedReplacement()
{
    if (!forwarded_backspace_release_pending_ ||
        !acknowledged_transaction_.active()) {
        return false;
    }
    forwarded_backspace_release_pending_ = false;
    if (uinput_device_.emitBackspace()) {
        return true;
    }
    acknowledged_transaction_.clear();
    return false;
}

void FcitxReplacementBackend::expectBarrierRelease()
{
    barrier_release_pending_ = true;
}

bool FcitxReplacementBackend::consumeBarrierRelease()
{
    if (!barrier_release_pending_) {
        return false;
    }
    barrier_release_pending_ = false;
    return true;
}

std::uint64_t FcitxReplacementBackend::finishAcknowledgedReplacement()
{
    if (!acknowledged_transaction_.active()) {
        return 0;
    }
    const std::uint64_t sequence =
        acknowledged_transaction_.sequenceId();
    const std::string commit_text(
        acknowledged_transaction_.commitText());
    acknowledged_transaction_.clear();
    if (!commit_text.empty()) {
        input_context_.commitString(commit_text);
    }
    last_sequence_id_ = sequence;
    return sequence;
}

} // namespace unilume::fcitx5
