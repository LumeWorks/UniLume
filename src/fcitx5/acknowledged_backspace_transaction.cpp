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
    ++acknowledged_presses_;
    return BackspaceAcknowledgement::forward_deletion;
}

BackspaceReleaseAcknowledgement
AcknowledgedBackspaceTransaction::acknowledgeRelease()
{
    if (!active_ || !press_acknowledged_) {
        return BackspaceReleaseAcknowledgement::unexpected;
    }
    press_acknowledged_ = false;
    if (acknowledged_presses_ < deletions_) {
        return BackspaceReleaseAcknowledgement::emit_next;
    }
    return strategy_ == DirectStrategy::guarded
               ? BackspaceReleaseAcknowledgement::complete_guarded
               : BackspaceReleaseAcknowledgement::complete_fast;
}

void AcknowledgedBackspaceTransaction::markPressDispatched()
{
    if (active_ && dispatched_presses_ < deletions_) {
        ++dispatched_presses_;
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
}

} // namespace unilume::fcitx5
