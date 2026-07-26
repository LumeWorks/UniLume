// SPDX-License-Identifier: GPL-2.0-or-later

#include "deterministic_backend.h"

#include "utf8_validation.h"

namespace unilume::integration::test {

DeterministicBackend::DeterministicBackend(BackendProfile profile)
    : profile_(profile)
{
    text_.reserve(profile_.text_reserve_bytes);
    if (profile_.text_reserve_bytes != 0) {
        // Commit the reserved pages before RSS sampling. Otherwise a soak
        // would report the simulated application's growing document buffer
        // as controller memory growth.
        text_.resize(profile_.text_reserve_bytes);
        text_.clear();
    }
}

bool DeterministicBackend::canReplace(
    std::int32_t delete_before_cursor) const
{
    if (delete_before_cursor < 0 ||
        !profile_.surrounding_text_available ||
        profile_.stale_surrounding_text ||
        profile_.invalid_surrounding_text ||
        profile_.cursor_misaligned) {
        return false;
    }
    return static_cast<std::size_t>(delete_before_cursor) <=
           text_character_count_;
}

platform::ReplacementStatus DeterministicBackend::requestReplacement(
    std::uint64_t sequence_id,
    std::int32_t delete_before_cursor,
    std::string_view commit_text)
{
    if (pending_.active || !canReplace(delete_before_cursor)) {
        return platform::ReplacementStatus::failed;
    }
    if (delete_before_cursor > 0 && profile_.fail_next_delete) {
        profile_.fail_next_delete = false;
        return platform::ReplacementStatus::failed;
    }
    if (!commit_text.empty() && profile_.fail_next_commit) {
        profile_.fail_next_commit = false;
        return platform::ReplacementStatus::failed;
    }
    // Ordinary direct commits are synchronous in the adapter contract. The
    // virtual delay models stale surrounding-text replacement only.
    if (profile_.delay_events == 0 || delete_before_cursor == 0) {
        if (!apply(delete_before_cursor, commit_text)) {
            return platform::ReplacementStatus::failed;
        }
        if (profile_.record_event_log) {
            event_log_.push_back({
                BackendEventKind::verified_replacement,
                sequence_id,
                delete_before_cursor,
                std::string(commit_text),
            });
        }
        ++applied_events_;
        return platform::ReplacementStatus::completed;
    }

    pending_ = {
        true,
        sequence_id,
        delete_before_cursor,
        std::string(commit_text),
        event_clock_ + profile_.delay_events,
        profile_.duplicate_next_callback,
        profile_.reorder_next_callback,
        profile_.drop_next_callback,
    };
    profile_.duplicate_next_callback = false;
    profile_.reorder_next_callback = false;
    profile_.drop_next_callback = false;
    return platform::ReplacementStatus::pending;
}

platform::ReplacementStatus
DeterministicBackend::requestFallbackCommit(
    std::uint64_t sequence_id,
    std::string_view commit_text)
{
    if (pending_.active || !core::isValidUtf8(commit_text)) {
        return platform::ReplacementStatus::failed;
    }
    if (!commit_text.empty() && profile_.fail_next_commit) {
        profile_.fail_next_commit = false;
        return platform::ReplacementStatus::failed;
    }
    if (!apply(0, commit_text)) {
        return platform::ReplacementStatus::failed;
    }
    if (profile_.record_event_log) {
        event_log_.push_back({
            BackendEventKind::fallback_commit,
            sequence_id,
            0,
            std::string(commit_text),
        });
    }
    ++applied_events_;
    return platform::ReplacementStatus::completed;
}

bool DeterministicBackend::cancel(std::uint64_t sequence_id)
{
    if (profile_.refuse_cancel ||
        !pending_.active || pending_.sequence_id != sequence_id) {
        return false;
    }
    pending_ = {};
    return true;
}

void DeterministicBackend::reset()
{
    pending_ = {};
    injected_.clear();
}

std::vector<BackendCompletion> DeterministicBackend::advance(
    std::size_t events)
{
    event_clock_ += events;
    std::vector<BackendCompletion> completions;
    completions.swap(injected_);
    if (!pending_.active || pending_.dropped ||
        pending_.due_event > event_clock_) {
        return completions;
    }

    const PendingReplacement pending = pending_;
    pending_ = {};
    const bool success =
        apply(pending.delete_before_cursor, pending.commit_text);
    if (success) {
        if (profile_.record_event_log) {
            event_log_.push_back({
                BackendEventKind::verified_replacement,
                pending.sequence_id,
                pending.delete_before_cursor,
                pending.commit_text,
            });
        }
        ++applied_events_;
    }
    if (pending.reorder_callback) {
        completions.push_back({pending.sequence_id + 1, true});
    }
    completions.push_back({pending.sequence_id, success});
    if (pending.duplicate_callback) {
        completions.push_back({pending.sequence_id, success});
    }
    return completions;
}

void DeterministicBackend::injectCompletion(std::uint64_t sequence_id,
                                            bool success)
{
    injected_.push_back({sequence_id, success});
}

bool DeterministicBackend::hasPending() const
{
    return pending_.active;
}

const std::string &DeterministicBackend::text() const
{
    return text_;
}

const std::vector<BackendEvent> &DeterministicBackend::eventLog() const
{
    return event_log_;
}

std::size_t DeterministicBackend::appliedEvents() const
{
    return applied_events_;
}

bool DeterministicBackend::apply(std::int32_t delete_before_cursor,
                                 std::string_view commit_text)
{
    if (delete_before_cursor < 0 || !core::isValidUtf8(commit_text) ||
        static_cast<std::size_t>(delete_before_cursor) >
            text_character_count_) {
        return false;
    }
    std::size_t position = text_.size();
    for (std::int32_t count = 0; count < delete_before_cursor; ++count) {
        position = previousCharacter(text_, position);
    }
    text_.erase(position);
    text_.append(commit_text);
    text_character_count_ -=
        static_cast<std::size_t>(delete_before_cursor);
    text_character_count_ += characterCount(commit_text);
    return true;
}

std::size_t DeterministicBackend::previousCharacter(
    std::string_view text,
    std::size_t position)
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

std::size_t DeterministicBackend::characterCount(std::string_view text)
{
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xc0) != 0x80) {
            ++count;
        }
    }
    return count;
}

} // namespace unilume::integration::test
