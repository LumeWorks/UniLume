// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "replacement_backend.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace unilume::integration::test {

struct BackendProfile {
    std::size_t delay_events{};
    bool surrounding_text_available{true};
    bool stale_surrounding_text{};
    bool invalid_surrounding_text{};
    bool cursor_misaligned{};
    bool fail_next_delete{};
    bool fail_next_commit{};
    bool duplicate_next_callback{};
    bool reorder_next_callback{};
    bool drop_next_callback{};
    bool refuse_cancel{};
    bool record_event_log{true};
    std::size_t text_reserve_bytes{};
};

struct BackendCompletion {
    std::uint64_t sequence_id{};
    bool success{};
};

enum class BackendEventKind {
    verified_replacement,
    fallback_commit,
};

struct BackendEvent {
    BackendEventKind kind{BackendEventKind::verified_replacement};
    std::uint64_t sequence_id{};
    std::int32_t delete_before_cursor{};
    std::string commit_text;
};

class DeterministicBackend final : public platform::ReplacementBackend {
public:
    explicit DeterministicBackend(BackendProfile profile = {});

    [[nodiscard]] bool canReplace(
        std::int32_t delete_before_cursor) const override;
    platform::ReplacementStatus requestReplacement(
        std::uint64_t sequence_id,
        std::int32_t delete_before_cursor,
        std::string_view commit_text) override;
    platform::ReplacementStatus requestFallbackCommit(
        std::uint64_t sequence_id,
        std::string_view commit_text) override;
    bool cancel(std::uint64_t sequence_id) override;
    void reset() override;

    std::vector<BackendCompletion> advance(std::size_t events = 1);
    void injectCompletion(std::uint64_t sequence_id, bool success);
    [[nodiscard]] bool hasPending() const;
    [[nodiscard]] const std::string &text() const;
    [[nodiscard]] const std::vector<BackendEvent> &eventLog() const;
    [[nodiscard]] std::size_t appliedEvents() const;

private:
    struct PendingReplacement {
        bool active{};
        std::uint64_t sequence_id{};
        std::int32_t delete_before_cursor{};
        std::string commit_text;
        std::size_t due_event{};
        bool duplicate_callback{};
        bool reorder_callback{};
        bool dropped{};
    };

    bool apply(std::int32_t delete_before_cursor,
               std::string_view commit_text);
    static std::size_t previousCharacter(std::string_view text,
                                         std::size_t position);
    static std::size_t characterCount(std::string_view text);

    BackendProfile profile_;
    std::string text_;
    std::vector<BackendEvent> event_log_;
    std::vector<BackendCompletion> injected_;
    PendingReplacement pending_;
    std::size_t event_clock_{};
    std::size_t applied_events_{};
    std::size_t text_character_count_{};
};

} // namespace unilume::integration::test
