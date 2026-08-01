// SPDX-License-Identifier: GPL-2.0-or-later

#include "acknowledged_backspace_transaction.h"
#include "uinput_backspace_device.h"

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
    ok &= expect(classifyUinputBatchWrite(-1, 96) ==
                     UinputBatchWriteStatus::no_events &&
                     classifyUinputBatchWrite(0, 96) ==
                     UinputBatchWriteStatus::no_events &&
                     classifyUinputBatchWrite(48, 96) ==
                     UinputBatchWriteStatus::partial &&
                     classifyUinputBatchWrite(96, 96) ==
                     UinputBatchWriteStatus::complete,
                 "uinput batch write result was misclassified");
    AcknowledgedBackspaceTransaction transaction;
    ok &= expect(
        transaction.acknowledge() == BackspaceAcknowledgement::unexpected,
        "idle transaction accepted a Backspace acknowledgement");
    ok &= expect(!transaction.prepare(
                     0, 1, "x", DirectStrategy::fast),
                 "zero sequence was accepted");
    ok &= expect(!transaction.prepare(
                     1, 0, "x", DirectStrategy::fast),
                 "zero deletion transaction was accepted");
    ok &= expect(!transaction.prepare(
                     1,
                     AcknowledgedBackspaceTransaction::maximum_deletions + 1,
                     "x", DirectStrategy::fast),
                 "oversized deletion transaction was accepted");
    ok &= expect(transaction.prepare(
                     42, 2, "ế", DirectStrategy::fast),
                 "valid acknowledged deletion was rejected");
    ok &= expect(transaction.active() &&
                     transaction.sequenceId() == 42 &&
                     transaction.commitText() == "ế",
                 "prepared deletion state was not retained exactly");
    ok &= expect(!transaction.prepare(
                     43, 1, "x", DirectStrategy::fast),
                 "active transaction was replaced");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::unexpected,
                 "undispatched Backspace was acknowledged");
    transaction.markPressDispatched();
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion,
                 "first deletion press was not acknowledged");
    ok &= expect(transaction.releasePending() &&
                     transaction.outstandingPresses() == 0,
                 "first deletion did not retain its release boundary");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::unexpected,
                 "second deletion press bypassed its release");
    ok &= expect(transaction.releasePending() &&
                     transaction.outstandingPresses() == 0,
                 "duplicate press corrupted the successor fence");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::emit_next,
                 "first deletion release did not request its successor");
    transaction.markPressDispatched();
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion,
                 "second deletion press was not acknowledged");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::complete_fast,
                 "final deletion release did not complete fast mode");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::unexpected,
                 "final deletion release was acknowledged twice");
    transaction.clear();
    ok &= expect(!transaction.active() && transaction.sequenceId() == 0 &&
                     transaction.commitText().empty(),
                 "clear retained transaction state");

    ok &= expect(transaction.prepare(
                     43, 1, "đ", DirectStrategy::guarded),
                 "valid guarded transaction was rejected");
    transaction.markPressDispatched();
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion,
                 "guarded deletion press was not forwarded");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::complete_guarded,
                 "guarded deletion release did not complete");
    return ok ? 0 : 1;
}
