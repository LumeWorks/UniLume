// SPDX-License-Identifier: GPL-2.0-or-later

#include "fcitx_replacement_backend.h"

#include "replacement_transport_contract.h"
#include "utf8_validation.h"
#include "verified_surrounding_snapshot.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx/surroundingtext.h>
#include <string>

namespace unilume::fcitx5 {

bool FcitxReplacementBackend::atomicReplacementAvailable() const
{
#if UNILUME_HAVE_FCITX_ATOMIC_REPLACEMENT
    const auto *atomic = dynamic_cast<
        const fcitx::AtomicSurroundingTextInputContext *>(&input_context_);
    return atomic &&
           atomic->supportsAtomicSurroundingTextReplacement();
#else
    return false;
#endif
}

bool FcitxReplacementBackend::requestAtomicReplacement(
    std::int32_t delete_before_cursor,
    std::string_view commit_text)
{
#if UNILUME_HAVE_FCITX_ATOMIC_REPLACEMENT
    auto *atomic = dynamic_cast<
        fcitx::AtomicSurroundingTextInputContext *>(&input_context_);
    return atomic && atomic->replaceSurroundingTextAtomically(
        -delete_before_cursor,
        static_cast<unsigned int>(delete_before_cursor),
        std::string(commit_text));
#else
    (void)delete_before_cursor;
    (void)commit_text;
    return false;
#endif
}

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
    observation_.atomic_transport = atomicReplacementAvailable();
    observation_.acknowledged_uinput =
        !observation_.atomic_transport && uinput_device_.available();
    if (poisoned_) {
        verified_ticket_.clear();
        return false;
    }
    if (observation_.atomic_transport &&
        !observation_.surrounding_available) {
        observation_.surrounding_bytes = 0;
        observation_.cursor_valid = true;
        observation_.within_resource_limit = true;
        observation_.utf8_valid = true;
        verified_ticket_.clear();
        return true;
    }
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
    observation_.atomic_transport = atomicReplacementAvailable();
    observation_.acknowledged_uinput =
        !observation_.atomic_transport && uinput_device_.available();
    observation_.cursor_valid = false;
    observation_.utf8_valid = false;
    observation_.within_resource_limit = false;
    if (delete_before_cursor < 0 ||
        (!observation_.atomic_transport &&
         !observation_.acknowledged_uinput &&
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
    if (observation_.atomic_transport &&
        !observation_.surrounding_available) {
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
        guarded_snapshot_ready_ = false;
        if (strategy_ == DirectStrategy::guarded &&
            observation_.surrounding_available) {
            guarded_snapshot_ready_ = true;
        }
        if (!acknowledged_transaction_.prepare(
                sequence_id, delete_count, commit_text, strategy_)) {
            acknowledged_transaction_.clear();
            return platform::ReplacementStatus::failed;
        }
        // The physical triggering press is still being processed by the
        // client. Start the synthetic sequence only when its release returns,
        // otherwise Chromium/Electron can observe the deletion out of order.
        initial_backspace_pending_ = true;
        return platform::ReplacementStatus::pending;
    }
    if (!requestAtomicReplacement(delete_before_cursor, commit_text)) {
        return platform::ReplacementStatus::failed;
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
        if (initial_backspace_pending_) {
            acknowledged_transaction_.clear();
            initial_backspace_pending_ = false;
            return true;
        }
        cancelled_backspace_presses_ +=
            acknowledged_transaction_.outstandingPresses();
        cancelled_backspace_release_pending_ =
            cancelled_backspace_release_pending_ ||
            acknowledged_transaction_.releasePending();
        acknowledged_transaction_.clear();
        poisoned_ = true;
        return false;
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
    guarded_snapshot_ready_ = false;
    uncertain_dispatch_ = false;
}

void FcitxReplacementBackend::setDirectStrategy(DirectStrategy strategy)
{
    if (strategy_ == strategy) {
        return;
    }
    reset();
    strategy_ = strategy;
    poisoned_ = false;
}

void FcitxReplacementBackend::clearFailure()
{
    poisoned_ = false;
    uncertain_dispatch_ = false;
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
    const UinputBatchWriteStatus write_status =
        uinput_device_.emitBackspaces(1);
    if (write_status == UinputBatchWriteStatus::complete) {
        acknowledged_transaction_.markPressDispatched();
        return true;
    }
    if (write_status == UinputBatchWriteStatus::partial) {
        uncertain_dispatch_ = true;
        poisoned_ = true;
    }
    acknowledged_transaction_.clear();
    return false;
}

BackspaceAcknowledgement FcitxReplacementBackend::acknowledgeBackspace()
{
    return acknowledged_transaction_.acknowledge();
}

BackspaceReleaseAcknowledgement
FcitxReplacementBackend::acknowledgeBackspaceRelease()
{
    const BackspaceReleaseAcknowledgement acknowledgement =
        acknowledged_transaction_.acknowledgeRelease();
    if (acknowledgement != BackspaceReleaseAcknowledgement::emit_next) {
        return acknowledgement;
    }
    const UinputBatchWriteStatus write_status =
        uinput_device_.emitBackspaces(1);
    if (write_status == UinputBatchWriteStatus::complete) {
        acknowledged_transaction_.markPressDispatched();
    } else {
        uncertain_dispatch_ = true;
        poisoned_ = true;
    }
    return acknowledgement;
}

bool FcitxReplacementBackend::consumeCancelledBackspace(bool release)
{
    if (release) {
        if (!cancelled_backspace_release_pending_) {
            return false;
        }
        cancelled_backspace_release_pending_ = false;
        return true;
    }
    if (cancelled_backspace_release_pending_) {
        // A duplicate or reordered press still belongs to the fenced batch.
        return true;
    }
    if (cancelled_backspace_presses_ == 0) {
        return false;
    }
    --cancelled_backspace_presses_;
    cancelled_backspace_release_pending_ = true;
    return true;
}

bool FcitxReplacementBackend::consumeUncertainDispatch()
{
    const bool uncertain = uncertain_dispatch_;
    uncertain_dispatch_ = false;
    return uncertain;
}

bool FcitxReplacementBackend::poisoned() const
{
    return poisoned_;
}

bool FcitxReplacementBackend::guardedBoundaryValid() const
{
    if (!guarded_snapshot_ready_) {
        return true;
    }
    const fcitx::SurroundingText &surrounding =
        input_context_.surroundingText();
    const SurroundingSnapshotValidation validation =
        validateSurroundingSnapshot(true, surrounding, 0);
    if (!validation.allowsReplacement()) {
        return false;
    }
    // Split frontends publish surrounding snapshots asynchronously, so an
    // older byte offset cannot be compared to the just-dispatched deletion.
    // Guarded still requires a live, bounded UTF-8 snapshot with a collapsed
    // valid cursor at the release boundary. Navigation is fenced before this
    // point by the key classifier.
    return validation.allowsReplacement();
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
