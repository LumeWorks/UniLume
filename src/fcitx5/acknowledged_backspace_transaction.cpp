// SPDX-License-Identifier: GPL-2.0-or-later

#include "acknowledged_backspace_transaction.h"

namespace unilume::fcitx5 {

bool AcknowledgedBackspaceTransaction::prepare(
    std::uint64_t sequence_id,
    std::size_t deletions,
    std::string_view commit_text)
{
    if (active_ || sequence_id == 0 || deletions == 0 ||
        deletions > maximum_deletions) {
        return false;
    }
    sequence_id_ = sequence_id;
    remaining_deletions_ = deletions;
    emitted_backspaces_ = deletions + 1;
    commit_text_.assign(commit_text);
    active_ = true;
    return true;
}

BackspaceAcknowledgement AcknowledgedBackspaceTransaction::acknowledge()
{
    if (!active_) {
        return BackspaceAcknowledgement::unexpected;
    }
    if (remaining_deletions_ != 0) {
        --remaining_deletions_;
        return BackspaceAcknowledgement::forward_deletion;
    }
    if (barrier_acknowledged_) {
        return BackspaceAcknowledgement::unexpected;
    }
    barrier_acknowledged_ = true;
    return BackspaceAcknowledgement::consume_barrier;
}

bool AcknowledgedBackspaceTransaction::active() const
{
    return active_;
}

std::size_t AcknowledgedBackspaceTransaction::emittedBackspaces() const
{
    return active_ ? emitted_backspaces_ : 0;
}

std::uint64_t AcknowledgedBackspaceTransaction::sequenceId() const
{
    return active_ ? sequence_id_ : 0;
}

std::string_view AcknowledgedBackspaceTransaction::commitText() const
{
    return active_ ? std::string_view{commit_text_} : std::string_view{};
}

void AcknowledgedBackspaceTransaction::clear()
{
    sequence_id_ = 0;
    remaining_deletions_ = 0;
    emitted_backspaces_ = 0;
    commit_text_.clear();
    active_ = false;
    barrier_acknowledged_ = false;
}

} // namespace unilume::fcitx5
