// SPDX-License-Identifier: GPL-2.0-or-later

#include "direct_commit_controller.h"
#include "split_transport_backend.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <cstdint>
#include <string>

namespace unilume::integration::test {
namespace {

// Outcome-containment fixture: wraps a DirectCommitController around a
// backend that delivers the logical delete and the commit as two independent
// messages (mirroring the P1 zero-preedit transport investigation). The
// assertions assert controller containment of the replacement outcome model:
// applied/not_applied/uncertain. They assert what the controller must
// contain; they do not claim the production atomic frontend performs partial
// mutations.
class SplitFixture {
public:
    explicit SplitFixture(SplitProfile profile)
        : backend_(profile), controller_(backend_)
    {
    }

    void type(char ch)
    {
        const core::KeyInput input{
            core::KeyKind::text,
            std::string_view{&ch, 1},
            false,
            false,
            false};
        const core::SubmissionStatus status = controller_.submit(input);
        if (status == core::SubmissionStatus::unhandled ||
            status == core::SubmissionStatus::passthrough) {
            backend_.forwardRaw(std::string_view{&ch, 1});
        }
    }

    void type(std::string_view text)
    {
        for (const char ch : text) {
            type(ch);
        }
    }

    void pressBackspace()
    {
        const core::KeyInput input{
            core::KeyKind::backspace,
            {},
            false,
            false,
            false};
        const core::SubmissionStatus status = controller_.submit(input);
        if (status == core::SubmissionStatus::unhandled) {
            backend_.forwardRaw(std::string_view{});
        }
    }

    void timeoutActive()
    {
        const std::uint64_t sequence = controller_.activeSequence();
        if (sequence != 0) {
            controller_.timeout(sequence);
        }
    }

    void completeStale(std::uint64_t sequence,
                       platform::ReplacementOutcome outcome)
    {
        controller_.complete(sequence, outcome);
    }

    void resetForFocus()
    {
        controller_.resetForFocus();
    }

    [[nodiscard]] std::uint64_t activeSequence() const
    {
        return controller_.activeSequence();
    }

    // Deliver currently pending events and settle every subsequent cascade
    // until the controller is idle again.
    void flush()
    {
        for (std::size_t step = 0; step < 100000; ++step) {
            deliverPending();
            if (!backend_.pending() &&
                !controller_.metrics().active_transaction &&
                controller_.metrics().queue_depth == 0) {
                return;
            }
        }
    }

    SplitTransportBackend &backend()
    {
        return backend_;
    }

    const core::TransactionMetrics &metrics() const
    {
        return controller_.metrics();
    }

    [[nodiscard]] const std::string &output() const
    {
        return backend_.text();
    }

private:
    void deliverPending()
    {
        if (!backend_.pending()) {
            return;
        }
        const std::uint64_t sequence = backend_.activeSequence();
        backend_.deliverEvents();
        controller_.complete(sequence, backend_.controllerOutcome());
    }

    SplitTransportBackend backend_;
    core::DirectCommitController controller_;
};

void expectUncertainContainment(
    Assertions &assertions,
    std::string_view label,
    SplitFixture &fixture)
{
    assertions.truth(std::string(label) + ": backend poisoned",
                     fixture.backend().poisoned());
    assertions.equal(std::string(label) + ": transaction inactive",
                     fixture.metrics().active_transaction ? 1 : 0, 0);
    assertions.equal(std::string(label) + ": uncertainty counted once",
                     static_cast<std::uint64_t>(
                         fixture.metrics().uncertain_outcome_count),
                     1);
    assertions.truth(std::string(label) + ": composition reset",
                     fixture.metrics().reset_count != 0);
}

} // namespace

void runOutcomeContainmentTests(Assertions &assertions)
{
    // Applied: the complete replacement is known to have been applied, the
    // engine state stays coherent and a queued continuation drains normally.
    {
        SplitFixture fixture{{SplitFailureKind::none}};
        fixture.type("phee");
        fixture.flush();
        assertions.equal("applied baseline word", fixture.output(),
                         "ph\u00ea");
        assertions.equal("applied queue drained",
                         fixture.metrics().queue_depth, 0);
        assertions.truth("applied backend not poisoned",
                         !fixture.backend().poisoned());
        assertions.equal("applied no uncertainty event",
                         fixture.metrics().uncertain_outcome_count, 0);
        assertions.truth("applied transaction committed",
                         fixture.metrics().completed_transactions != 0);
    }

    // Not applied: the backend rejected before any mutation was dispatched.
    // No tentative input is confirmed, the engine resets safely and nothing
    // is silently dropped (the queue stays empty with no uncertainty).
    {
        SplitFixture fixture{
            {SplitFailureKind::none, SplitAckMode::ack_on_commit,
             /* cancel_refused */ false, /* reject_before_request */ true}};
        fixture.type("cha");
        fixture.type("f");
        fixture.flush();
        // The rejected edit is not applied; the triggering key itself is
        // returned to the application unchanged (no silent mutation, no
        // silently dropped input).
        assertions.equal("rejected-before-dispatch preserves raw key",
                         fixture.output(), "chaf");
        assertions.equal("rejected transaction not confirmed",
                         fixture.metrics().completed_transactions, 0);
        assertions.equal("rejected has no uncertainty event",
                         fixture.metrics().uncertain_outcome_count, 0);
        assertions.truth("rejected did not poison the backend",
                         !fixture.backend().poisoned());
        assertions.truth("rejected reset the composition",
                         fixture.metrics().reset_count != 0);
        assertions.equal("rejected queue empty",
                         fixture.metrics().queue_depth, 0);
    }

    // Uncertain: delete-only (deletion applied, commit never delivered).
    {
        SplitFixture fixture{
            {SplitFailureKind::drop_commit, SplitAckMode::ack_on_commit}};
        fixture.type("phee");
        fixture.flush();
        expectUncertainContainment(assertions, "delete-only", fixture);
    }

    // Uncertain: commit-only (commit applied, deletion never applied).
    {
        SplitFixture fixture{
            {SplitFailureKind::drop_delete, SplitAckMode::ack_on_commit}};
        fixture.type("phee");
        fixture.flush();
        expectUncertainContainment(assertions, "commit-only", fixture);
    }

    // Uncertain: the commit landing before the delete (invalid ordering).
    {
        SplitFixture fixture{
            {SplitFailureKind::remove_after_commit, SplitAckMode::ack_on_commit}};
        fixture.type("phee");
        fixture.flush();
        expectUncertainContainment(assertions, "reordered", fixture);
    }

    // Uncertain: deletion applied against a stale visible range.
    {
        SplitFixture fixture{
            {SplitFailureKind::stale_delete, SplitAckMode::ack_on_commit}};
        fixture.type("chaf");
        fixture.flush();
        expectUncertainContainment(assertions, "stale", fixture);
    }

    // Uncertain: a dispatched transaction times out and cancellation cannot
    // guarantee zero mutation.
    {
        SplitFixture fixture{
            {SplitFailureKind::none, SplitAckMode::ack_on_commit,
             /* cancel_refused */ true, false}};
        fixture.type("phe");
        fixture.type(std::string_view{"e"});
        fixture.timeoutActive();
        fixture.flush();
        expectUncertainContainment(assertions, "timeout-after-dispatch",
                                   fixture);
    }

    // Uncertain: a duplicate completion arriving after the outcome must be a
    // stale callback, not a second uncertainty event.
    {
        SplitFixture fixture{
            {SplitFailureKind::drop_commit, SplitAckMode::ack_on_commit}};
        fixture.type("phee");
        fixture.flush();
        const std::uint64_t uncertainty_before =
            fixture.metrics().uncertain_outcome_count;
        const std::uint64_t stale_sequence = fixture.activeSequence();
        fixture.completeStale(stale_sequence,
                              platform::ReplacementOutcome::applied);
        assertions.equal("stale duplicate does not re-raise uncertainty",
                         fixture.metrics().uncertain_outcome_count,
                         uncertainty_before);
        assertions.truth("stale duplicate counted stale",
                         fixture.metrics().stale_result_count != 0);
        assertions.equal("stale duplicate did not start a transaction",
                         fixture.metrics().active_transaction ? 1 : 0, 0);
    }

    // Uncertain: a completion arriving after reset must be ignored.
    {
        SplitFixture fixture{
            {SplitFailureKind::drop_commit, SplitAckMode::ack_on_commit}};
        fixture.type("phee");
        fixture.flush();
        const std::uint64_t reset_sequence = fixture.activeSequence();
        fixture.resetForFocus();
        fixture.completeStale(reset_sequence,
                              platform::ReplacementOutcome::applied);
        assertions.truth("stale-after-reset counted stale",
                         fixture.metrics().stale_result_count != 0);
        assertions.equal("stale-after-reset confirmed nothing",
                         fixture.metrics().completed_transactions, 0);
    }

    // Uncertain + mixed queued input: printable keys are preserved through
    // commit-only literal fallback; a queued Backspace is discarded instead
    // of being guessed as text, and a flag tracks the discarded input.
    {
        SplitFixture fixture{
            {SplitFailureKind::drop_delete, SplitAckMode::ack_on_commit}};
        fixture.type("phe");
        fixture.type(std::string_view{"e"});  // pending failing transaction
        fixture.type(std::string_view{"x"});
        fixture.pressBackspace();
        fixture.type(std::string_view{"y"});
        fixture.flush();
        assertions.equal("mixed queue uncertainty counted once",
                         fixture.metrics().uncertain_outcome_count, 1);
        assertions.truth("mixed queue backend was poisoned",
                         fixture.backend().poisoned());
        assertions.equal("mixed queue Backspace discarded",
                         fixture.metrics().discarded_queued_input_count, 1);
        assertions.equal("mixed queue drained",
                         fixture.metrics().queue_depth, 0);
        assertions.equal("mixed queue preserved literal text",
                         fixture.output(), "phe\u00eaxy");
    }

    // After an uncertain terminal no new delete-surrounding direct
    // replacement may start from the stale composition.
    {
        SplitFixture fixture{
            {SplitFailureKind::drop_commit, SplitAckMode::ack_on_commit}};
        fixture.type("phee");
        fixture.flush();
        const std::uint64_t completed_before =
            fixture.metrics().completed_transactions;
        fixture.type("xy");
        assertions.equal("no new direct replacement after poison",
                         fixture.metrics().completed_transactions,
                         completed_before);
    }

    // Contract regression test (Issue #119): a uinput fast-sentinel ACK that
    // completes the full press/release/commit cycle must NOT be classified
    // as applied.  Only a real atomic guarantee may report applied.
    // The uinput_transport flag simulates a non-atomic transport where all
    // synthetic events succeed but no authoritative verification exists.
    {
        SplitFixture fixture{
            {SplitFailureKind::none, SplitAckMode::ack_on_commit,
             /* cancel_refused */ false, /* reject_before_request */ false,
             /* uinput_transport */ true}};
        fixture.type("phee");
        fixture.flush();
        assertions.equal("uinput ACK is uncertain, not applied",
                         fixture.metrics().completed_transactions, 0);
        expectUncertainContainment(assertions, "uinput-fast-sentinel",
                                   fixture);
    }

    // Contract regression test (Issue #119): a guarded uinput ACK with a
    // validated surrounding snapshot must still be uncertain.  A valid
    // snapshot is not sufficient proof by itself.
    {
        SplitFixture fixture{
            {SplitFailureKind::none, SplitAckMode::ack_on_both,
             /* cancel_refused */ false, /* reject_before_request */ false,
             /* uinput_transport */ true}};
        fixture.type("phee");
        fixture.flush();
        assertions.equal("guarded uinput ACK is uncertain, not applied",
                         fixture.metrics().completed_transactions, 0);
        expectUncertainContainment(assertions, "uinput-guarded-sentinel",
                                   fixture);
    }

    // Contract test: a duplicate uinput ACK arriving after the uncertain
    // terminal must not increase the uncertainty metric a second time.
    {
        SplitFixture fixture{
            {SplitFailureKind::none, SplitAckMode::ack_on_commit,
             /* cancel_refused */ false, /* reject_before_request */ false,
             /* uinput_transport */ true}};
        fixture.type("phee");
        fixture.flush();
        const std::uint64_t uncertainty_before =
            fixture.metrics().uncertain_outcome_count;
        const std::uint64_t stale_sequence = fixture.activeSequence();
        fixture.completeStale(stale_sequence,
                              platform::ReplacementOutcome::applied);
        assertions.equal("uinput duplicate ACK no second uncertainty",
                         fixture.metrics().uncertain_outcome_count,
                         uncertainty_before);
        assertions.truth("uinput duplicate ACK counted stale",
                         fixture.metrics().stale_result_count != 0);
    }
}

} // namespace unilume::integration::test