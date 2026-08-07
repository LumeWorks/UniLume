// SPDX-License-Identifier: GPL-2.0-or-later

#include "acknowledged_backspace_transaction.h"

namespace unilume::fcitx5 {

bool AcknowledgedBackspaceTransaction::prepare(
    std::uint64_t sequence_id,
    std::size_t deletions,
    std::string_view commit_text,
    DirectStrategy strategy)
{
    if (active_ || sequence_id == 0 || deletions == 0 ||
        deletions > maximum_deletions) {
        return false;
    }
    sequence_id_ = sequence_id;
    deletions_ = deletions;
    commit_text_.assign(commit_text);
    strategy_ = strategy;
    active_ = true;
    return true;
}

BackspaceAcknowledgement AcknowledgedBackspaceTransaction::acknowledge()
{
    if (!active_) {
        return BackspaceAcknowledgement::unexpected;
    }
    if (press_acknowledged_ ||
        acknowledged_presses_ >= dispatched_presses_ ||
        acknowledged_presses_ > deletions_) {
        return BackspaceAcknowledgement::unexpected;
    }
    press_acknowledged_ = true;
    sentinel_press_acknowledged_ = acknowledged_presses_ == deletions_;
    ++acknowledged_presses_;
    if (!sentinel_press_acknowledged_) {
        return BackspaceAcknowledgement::forward_deletion;
    }
    return strategy_ == DirectStrategy::fast
               ? BackspaceAcknowledgement::consume_sentinel_fast
               : BackspaceAcknowledgement::consume_sentinel_guarded;
}

BackspaceReleaseAcknowledgement
AcknowledgedBackspaceTransaction::acknowledgeRelease()
{
    if (!active_ || !press_acknowledged_) {
        return BackspaceReleaseAcknowledgement::unexpected;
    }
    press_acknowledged_ = false;
    if (!sentinel_press_acknowledged_) {
        return BackspaceReleaseAcknowledgement::forward_deletion;
    }
    sentinel_press_acknowledged_ = false;
    return strategy_ == DirectStrategy::guarded
               ? BackspaceReleaseAcknowledgement::complete_guarded
               : BackspaceReleaseAcknowledgement::consume_sentinel;
}

void AcknowledgedBackspaceTransaction::markPressDispatched()
{
    const std::size_t total_presses = deletions_ + 1;
    if (active_ && dispatched_presses_ < total_presses) {
        ++dispatched_presses_;
    }
}

void AcknowledgedBackspaceTransaction::markPressesDispatched(std::size_t count)
{
    const std::size_t total_presses = deletions_ + 1;
    if (active_) {
        dispatched_presses_ = std::min(count, total_presses);
    }
}

bool AcknowledgedBackspaceTransaction::active() const
{
    return active_;
}

std::uint64_t AcknowledgedBackspaceTransaction::sequenceId() const
{
    return active_ ? sequence_id_ : 0;
}

std::size_t AcknowledgedBackspaceTransaction::deletions() const
{
    return active_ ? deletions_ : 0;
}

std::string_view AcknowledgedBackspaceTransaction::commitText() const
{
    return active_ ? std::string_view{commit_text_} : std::string_view{};
}

std::size_t AcknowledgedBackspaceTransaction::outstandingPresses() const
{
    if (!active_ || dispatched_presses_ < acknowledged_presses_) {
        return 0;
    }
    return dispatched_presses_ - acknowledged_presses_;
}

bool AcknowledgedBackspaceTransaction::releasePending() const
{
    return active_ && press_acknowledged_;
}

void AcknowledgedBackspaceTransaction::clear()
{
    sequence_id_ = 0;
    deletions_ = 0;
    acknowledged_presses_ = 0;
    dispatched_presses_ = 0;
    commit_text_.clear();
    active_ = false;
    press_acknowledged_ = false;
    sentinel_press_acknowledged_ = false;
}

} // namespace unilume::fcitx5
