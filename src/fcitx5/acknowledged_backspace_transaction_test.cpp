// SPDX-License-Identifier: GPL-2.0-or-later

#include "acknowledged_backspace_transaction.h"

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main()
{
    using namespace unilume::fcitx5;
    bool ok = true;
    AcknowledgedBackspaceTransaction transaction;
    ok &= expect(
        transaction.acknowledge() == BackspaceAcknowledgement::unexpected,
        "idle transaction accepted a Backspace acknowledgement");
    ok &= expect(!transaction.prepare(0, 1, "x"),
                 "zero sequence was accepted");
    ok &= expect(!transaction.prepare(1, 0, "x"),
                 "zero deletion transaction was accepted");
    ok &= expect(!transaction.prepare(
                     1,
                     AcknowledgedBackspaceTransaction::maximum_deletions + 1,
                     "x"),
                 "oversized deletion transaction was accepted");
    ok &= expect(transaction.prepare(42, 2, "ế"),
                 "valid acknowledged deletion was rejected");
    ok &= expect(transaction.active() &&
                     transaction.sequenceId() == 42 &&
                     transaction.emittedBackspaces() == 3 &&
                     transaction.commitText() == "ế",
                 "prepared deletion state was not retained exactly");
    ok &= expect(!transaction.prepare(43, 1, "x"),
                 "active transaction was replaced");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion &&
                     transaction.acknowledge() ==
                         BackspaceAcknowledgement::forward_deletion,
                 "deletion Backspaces were not forwarded exactly");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::consume_barrier,
                 "final synchronization barrier was not consumed");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::unexpected,
                 "synchronization barrier was acknowledged twice");
    transaction.clear();
    ok &= expect(!transaction.active() && transaction.sequenceId() == 0 &&
                     transaction.commitText().empty(),
                 "clear retained transaction state");
    return ok ? 0 : 1;
}
