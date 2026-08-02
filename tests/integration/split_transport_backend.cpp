// SPDX-License-Identifier: GPL-2.0-or-later

#include "split_transport_backend.h"

namespace unilume::integration::test {

namespace {

std::size_t previousCharacter(std::string_view text, std::size_t position)
{
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    return position;
}

std::size_t characterCount(std::string_view text)
{
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xc0) != 0x80) {
            ++count;
        }
    }
    return count;
}

} // namespace

SplitTransportBackend::SplitTransportBackend(SplitProfile profile)
    : profile_(profile)
{
}

bool SplitTransportBackend::canReplace(
    std::int32_t delete_before_cursor) const
{
    return !poisoned_ && delete_before_cursor >= 0 &&
           static_cast<std::size_t>(delete_before_cursor) <=
               characterCount(text_);
}

platform::ReplacementStatus SplitTransportBackend::requestReplacement(
    std::uint64_t sequence_id,
    std::int32_t delete_before_cursor,
    std::string_view commit_text)
{
    if (pending_ || delete_before_cursor < 0) {
        return platform::ReplacementStatus::failed;
    }
    if (profile_.reject_before_request || poisoned_) {
        last_sequence_id_ = sequence_id;
        return platform::ReplacementStatus::failed;
    }
    sequence_id_ = sequence_id;
    delete_before_cursor_ = delete_before_cursor;
    commit_text_.assign(commit_text);
    pending_ = true;
    commit_delivered_ = false;
    delete_applied_ = false;
    commit_applied_ = false;
    fully_consistent_ = false;
    return platform::ReplacementStatus::pending;
}

platform::ReplacementStatus
SplitTransportBackend::requestFallbackCommit(
    std::uint64_t sequence_id,
    std::string_view commit_text)
{
    if (pending_) {
        return platform::ReplacementStatus::failed;
    }
    if (commit_text.empty()) {
        return platform::ReplacementStatus::completed;
    }
    text_.append(commit_text);
    last_sequence_id_ = sequence_id;
    return platform::ReplacementStatus::completed;
}

bool SplitTransportBackend::cancel(std::uint64_t sequence_id)
{
    if (profile_.cancel_refused) {
        return false;
    }
    if (!pending_ || sequence_id != sequence_id_) {
        return false;
    }
    pending_ = false;
    return true;
}

void SplitTransportBackend::reset()
{
    pending_ = false;
    sequence_id_ = 0;
    delete_before_cursor_ = 0;
    commit_text_.clear();
    commit_delivered_ = false;
    delete_applied_ = false;
    commit_applied_ = false;
}

const std::string &SplitTransportBackend::text() const
{
    return text_;
}

void SplitTransportBackend::forwardRaw(std::string_view text)
{
    text_.append(text);
}

bool SplitTransportBackend::apply(
    std::int32_t delete_before_cursor,
    std::string_view insert)
{
    if (delete_before_cursor < 0 ||
        static_cast<std::size_t>(delete_before_cursor) >
            characterCount(text_)) {
        return false;
    }
    std::size_t position = text_.size();
    for (std::int32_t count = 0; count < delete_before_cursor; ++count) {
        position = previousCharacter(text_, position);
    }
    text_.erase(position);
    text_.append(insert);
    return true;
}

void SplitTransportBackend::deliverEvents()
{
    if (!pending_) {
        return;
    }
    switch (profile_.failure) {
    case SplitFailureKind::none:
        // delete-then-insert, order verified, both succeed.
        delete_applied_ = apply(delete_before_cursor_, commit_text_);
        commit_applied_ = true;
        fully_consistent_ = true;
        break;
    case SplitFailureKind::drop_delete:
        // delete message lost; the insert still lands: partial mutation.
        delete_applied_ = false;
        commit_applied_ = true;
        fully_consistent_ = false;
        if (!commit_text_.empty()) {
            text_.append(commit_text_);
        }
        break;
    case SplitFailureKind::drop_commit:
        // deletion applied; commit message lost: partial mutation.
        delete_applied_ = apply(delete_before_cursor_, std::string_view{});
        commit_applied_ = false;
        fully_consistent_ = false;
        break;
    case SplitFailureKind::remove_after_commit:
        // Insert lands first, then the delete removes the trailing inserted
        // text; ordering is invalid even though the primitive calls succeed.
        commit_applied_ = true;
        if (!commit_text_.empty()) {
            text_.append(commit_text_);
        }
        delete_applied_ =
            apply(delete_before_cursor_, std::string_view{});
        fully_consistent_ = false;
        break;
    case SplitFailureKind::stale_delete: {
        // The deletion range no longer matches the visible text, but the
        // transport still acknowledges and the commit lands: stale mutation.
        delete_applied_ = false;
        commit_applied_ = true;
        fully_consistent_ = false;
        if (!commit_text_.empty()) {
            text_.append(commit_text_);
        }
        break;
    }
    }
    commit_delivered_ = profile_.failure != SplitFailureKind::drop_commit;
    last_sequence_id_ = sequence_id_;
    pending_ = false;
}

bool SplitTransportBackend::pending() const
{
    return pending_;
}

std::uint64_t SplitTransportBackend::activeSequence() const
{
    return pending_ ? sequence_id_ : 0;
}

platform::ReplacementOutcome SplitTransportBackend::controllerOutcome() const
{
    // A uinput transport has no atomic guarantee: even when all synthetic
    // events are delivered successfully, the ACK does not prove the target
    // application applied the delete-and-insert.  Per Issue #119, this must
    // be uncertain, not applied.
    if (profile_.uinput_transport) {
        return platform::ReplacementOutcome::uncertain;
    }
    if (fully_consistent_ && delete_applied_ && commit_applied_) {
        return platform::ReplacementOutcome::applied;
    }
    if (fully_consistent_ && !delete_applied_ && !commit_applied_) {
        return platform::ReplacementOutcome::not_applied;
    }
    // Partial application, unknown ordering or a stale range is never
    // classified as a clean terminal result.
    return platform::ReplacementOutcome::uncertain;
}

void SplitTransportBackend::markUncertainOutcome()
{
    poisoned_ = true;
}

bool SplitTransportBackend::poisoned() const
{
    return poisoned_;
}

std::int32_t SplitTransportBackend::pendingDeleteBeforeCursor() const
{
    return pending_ ? delete_before_cursor_ : 0;
}

std::string_view SplitTransportBackend::pendingCommitText() const
{
    return pending_ ? std::string_view{commit_text_} : std::string_view{};
}

} // namespace unilume::integration::test