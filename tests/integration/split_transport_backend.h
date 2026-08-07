// SPDX-License-Identifier: GPL-2.0-or-later

// A test-only platform::ReplacementBackend that models a split transport:
// the logical delete and the commit are delivered to the simulated
// application as two independent messages, whose order and outcome the test
// drives explicitly.
//
// Outcome-containment harness for the P1/P2 zero-preedit investigation.
// NOTE: this models a transport that can apply replacement messages
// partially or in an unknown order. It DOES NOT prove that the patched
// production frontends currently exhibit this: the atomic path treats
// delete+commit as one synchronous request and the acknowledged uinput path
// uses a press/release pair, neither of which can fail, reorder or drop the
// two messages independently in the way this backend exercises. The harness
// describes what the controller must be able to contain when such partial
// outcomes occur.

#pragma once

#include "replacement_backend.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace unilume::integration::test {

enum class SplitFailureKind {
    none,
    // delete message is dropped; the insert still appears.
    drop_delete,
    // commit message is dropped; deletion applies but insert never appears.
    drop_commit,
    // delete delivered after the commit (reordered).
    remove_after_commit,
    // delete applies to a stale range; visible text unchanged, commit lands.
    stale_delete,
};

enum class SplitAckMode {
    // one logged result is "applied" once the commit message returns.
    ack_on_commit,
    // the result is "applied" only when both messages have been delivered.
    ack_on_both,
};

struct SplitProfile {
    SplitFailureKind failure{SplitFailureKind::none};
    SplitAckMode ack{SplitAckMode::ack_on_commit};
    // Backend cancellation is refused once a mutation may have been
    // dispatched (models "cancel cannot guarantee zero mutation").
    bool cancel_refused{};
    // Simulates a backend rejection before any mutation may be dispatched.
    bool reject_before_request{};
    // Simulates a non-atomic uinput transport: all synthetic events are
    // delivered successfully, but there is no atomic guarantee that the
    // target application applied the delete-and-insert.  Per Issue #119,
    // a uinput ACK must be classified as uncertain, not applied.
    bool uinput_transport{};
};

class SplitTransportBackend final : public platform::ReplacementBackend {
public:
    explicit SplitTransportBackend(SplitProfile profile = {});

    // platform::ReplacementBackend contract
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
    void markUncertainOutcome() override;
    void reset() override;

    [[nodiscard]] const std::string &text() const;
    void forwardRaw(std::string_view text);
    void deliverEvents();

    [[nodiscard]] bool pending() const;
    [[nodiscard]] std::uint64_t activeSequence() const;
    [[nodiscard]] platform::ReplacementOutcome controllerOutcome() const;
    [[nodiscard]] bool poisoned() const;
    // The edit the engine asked for, visible before deliverEvents().
    [[nodiscard]] std::int32_t pendingDeleteBeforeCursor() const;
    [[nodiscard]] std::string_view pendingCommitText() const;

private:
    bool apply(std::int32_t delete_before_cursor, std::string_view insert);

    SplitProfile profile_;
    std::string text_;
    std::string commit_text_;
    std::uint64_t last_sequence_id_{};
    std::uint64_t sequence_id_{};
    std::int32_t delete_before_cursor_{};
    bool pending_{};
    bool commit_delivered_{};
    bool delete_applied_{};
    bool commit_applied_{};
    bool fully_consistent_{};
    bool poisoned_{};
};

} // namespace unilume::integration::test