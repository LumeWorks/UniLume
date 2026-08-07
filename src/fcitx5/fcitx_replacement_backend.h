// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "replacement_backend.h"
#include "acknowledged_backspace_transaction.h"
#include "uinput_backspace_device.h"
#include "verified_surrounding_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <fcitx/inputcontext.h>

namespace unilume::fcitx5 {

struct ReplacementObservation {
    std::uint64_t sequence_id{};
    std::int32_t delete_before_cursor{};
    std::size_t commit_bytes{};
    std::size_t surrounding_bytes{};
    std::uint64_t generation{};
    bool surrounding_available{};
    bool cursor_valid{};
    bool utf8_valid{};
    bool within_resource_limit{};
    bool atomic_transport{true};
    bool acknowledged_uinput{};
};

class FcitxReplacementBackend final
    : public platform::ReplacementBackend {
public:
    FcitxReplacementBackend(fcitx::InputContext &input_context,
                            UinputBackspaceDevice &uinput_device);

    [[nodiscard]] bool supportsDirectReplacement() const;
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
    void setDirectStrategy(DirectStrategy strategy);
    void clearFailure();
    [[nodiscard]] const ReplacementObservation &lastObservation() const;
    [[nodiscard]] bool acknowledgedDeletionPending() const;
    [[nodiscard]] bool initialBackspacePending() const;
    [[nodiscard]] bool startAcknowledgedReplacement();
    [[nodiscard]] BackspaceAcknowledgement acknowledgeBackspace();
    [[nodiscard]] BackspaceReleaseAcknowledgement
    acknowledgeBackspaceRelease();
    [[nodiscard]] bool consumeFastSentinelRelease();
    [[nodiscard]] bool fastSentinelReleasePending() const;
    [[nodiscard]] bool consumeCancelledBackspace(bool release);
    [[nodiscard]] bool consumeUncertainDispatch();
    [[nodiscard]] bool poisoned() const;
    [[nodiscard]] bool guardedBoundaryValid() const;
    [[nodiscard]] bool guardedSnapshotReady() const;
    [[nodiscard]] std::uint64_t finishAcknowledgedReplacement();

private:
    [[nodiscard]] bool atomicReplacementAvailable() const;
    [[nodiscard]] bool requestAtomicReplacement(
        std::int32_t delete_before_cursor,
        std::string_view commit_text);

    fcitx::InputContext &input_context_;
    UinputBackspaceDevice &uinput_device_;
    std::uint64_t last_sequence_id_{};
    std::uint64_t generation_{1};
    mutable ReplacementObservation observation_;
    mutable VerifiedSurroundingTicket verified_ticket_;
    AcknowledgedBackspaceTransaction acknowledged_transaction_;
    bool initial_backspace_pending_{};
    bool guarded_snapshot_ready_{};
    bool fast_sentinel_release_pending_{};
    std::size_t cancelled_backspace_presses_{};
    bool cancelled_backspace_release_pending_{};
    bool uncertain_dispatch_{};
    bool poisoned_{};
    DirectStrategy strategy_{DirectStrategy::fast};
};

} // namespace unilume::fcitx5
