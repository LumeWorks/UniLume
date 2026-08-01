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
                     transaction.commitText() == "ế" &&
                     transaction.pressesToDispatch() == 3,
                 "prepared deletion state was not retained exactly");
    ok &= expect(!transaction.prepare(
                     43, 1, "x", DirectStrategy::fast),
                 "active transaction was replaced");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::unexpected,
                 "undispatched Backspace was acknowledged");
    transaction.markPressesDispatched(3);
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion,
                 "first deletion press was not acknowledged");
    ok &= expect(transaction.releasePending() &&
                     transaction.outstandingPresses() == 2,
                 "first deletion did not retain remaining dispatched presses");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::unexpected,
                 "second deletion press bypassed its release");
    ok &= expect(transaction.releasePending() &&
                     transaction.outstandingPresses() == 2,
                 "duplicate press corrupted the successor fence");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::forward_deletion,
                 "first deletion release was not forwarded");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion,
                 "second deletion press was not acknowledged");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::forward_deletion,
                 "second deletion release was not forwarded");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::consume_sentinel_fast,
                 "fast sentinel press was not consumed at commit boundary");
    ok &= expect(transaction.releasePending() &&
                     transaction.outstandingPresses() == 0,
                 "fast sentinel did not leave exactly one release pending");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::consume_sentinel,
                 "fast sentinel release was not consumed");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::unexpected,
                 "sentinel release was acknowledged twice");
    transaction.clear();
    ok &= expect(!transaction.active() && transaction.sequenceId() == 0 &&
                     transaction.commitText().empty(),
                 "clear retained transaction state");

    ok &= expect(transaction.prepare(
                     43, 1, "đ", DirectStrategy::guarded),
                 "valid guarded transaction was rejected");
    transaction.markPressesDispatched(2);
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::forward_deletion,
                 "guarded deletion press was not forwarded");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::forward_deletion,
                 "guarded deletion release was not forwarded");
    ok &= expect(transaction.acknowledge() ==
                     BackspaceAcknowledgement::consume_sentinel_guarded,
                 "guarded sentinel press committed too early");
    ok &= expect(transaction.acknowledgeRelease() ==
                     BackspaceReleaseAcknowledgement::complete_guarded,
                 "guarded sentinel release did not complete");
    return ok ? 0 : 1;
}
