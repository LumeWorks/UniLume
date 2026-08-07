// SPDX-License-Identifier: GPL-2.0-or-later

// Policy/session regression for the production Direct/Off contract. The
// simulation deliberately has no preedit controller: an unavailable direct
// backend leaves the key to the frontend, while an available backend uses the
// real DirectCommitController.

#include "deterministic_backend.h"
#include "direct_commit_controller.h"
#include "input_mode_policy.h"
#include "preedit_fallback_controller.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace unilume::integration::test {
namespace {

class BrowserInputSession {
public:
    BrowserInputSession()
        : backend_({.surrounding_text_available = true,
                    .record_event_log = true}),
          direct_(backend_)
    {
    }

    platform::InputPath synchronize(bool direct_available)
    {
        direct_available_ = direct_available;
        const platform::InputPath previous = policy_.path();
        const platform::InputPath current =
            policy_.observe(direct_available);
        if (previous == platform::InputPath::direct &&
            current == platform::InputPath::preedit) {
            direct_.resetForFocus();
        } else if (previous == platform::InputPath::preedit &&
                   current == platform::InputPath::direct) {
            commitPreedit();
        }
        return current;
    }

    void submitString(std::string_view input)
    {
        for (const char key : input) {
            synchronize(direct_available_);
            const core::KeyInput event{
                core::KeyKind::text,
                std::string_view{&key, 1},
                false, false, false,
            };
            if (policy_.path() == platform::InputPath::preedit) {
                const core::PreeditAction action = preedit_.submit(event);
                if (!action.commit_text.empty() &&
                    !backend_.forwardRaw(0, action.commit_text)) {
                    throw std::runtime_error("preedit handoff failed");
                }
            } else if (policy_.path() == platform::InputPath::off) {
                direct_.resetForFocus();
                if (!backend_.forwardRaw(0, event.text)) {
                    throw std::runtime_error("raw passthrough failed");
                }
            } else {
                const core::SubmissionStatus status = direct_.submit(event);
                if (status == core::SubmissionStatus::unhandled ||
                    status == core::SubmissionStatus::passthrough) {
                    if (!backend_.forwardRaw(0, event.text)) {
                        throw std::runtime_error("raw passthrough failed");
                    }
                }
            }
            drain();
        }
    }

    void shortcutBoundary()
    {
        commitPreedit();
        direct_.resetForFocus();
        policy_.resetForCompositionEnd();
    }

    void focusReset()
    {
        commitPreedit();
        direct_.resetForFocus();
        policy_.reset();
    }

    [[nodiscard]] platform::InputPath path() const { return policy_.path(); }
    [[nodiscard]] std::string output() const
    {
        return backend_.text() + std::string(preedit_.preedit());
    }
    [[nodiscard]] std::size_t backendEventCount() const
    {
        return backend_.eventLog().size();
    }
    [[nodiscard]] const core::TransactionMetrics &metrics() const
    {
        return direct_.metrics();
    }

private:
    void commitPreedit()
    {
        if (!preedit_.preedit().empty()) {
            backend_.forwardRaw(0, preedit_.preedit());
        }
        preedit_.reset();
    }

    void drain()
    {
        for (std::size_t step = 0; step < 10000; ++step) {
            if (!backend_.hasPending() &&
                direct_.transactionState() == core::TransactionState::idle &&
                direct_.metrics().queue_depth == 0) {
                return;
            }
            if (backend_.hasPending()) {
                for (const BackendCompletion &completion : backend_.advance()) {
                    direct_.complete(
                        completion.sequence_id,
                        completion.success
                            ? platform::ReplacementOutcome::applied
                            : platform::ReplacementOutcome::not_applied);
                }
            } else if (direct_.activeSequence() != 0) {
                direct_.timeout(direct_.activeSequence());
            }
        }
        throw std::runtime_error("session queue did not drain");
    }

    DeterministicBackend backend_;
    platform::InputModePolicy policy_;
    core::DirectCommitController direct_;
    core::PreeditFallbackController preedit_{
        UL_INPUT_METHOD_TELEX,
        core::PreeditCommitPolicy::composition_boundary};
    bool direct_available_{};
};

} // namespace

void runBrowserInputSessionTests(Assertions &assertions)
{
    {
        BrowserInputSession unavailable;
        assertions.truth("session starts with unknown path",
                         unavailable.path() == platform::InputPath::unknown);
        unavailable.synchronize(false);
        assertions.truth("unavailable backend selects passthrough",
                         unavailable.path() == platform::InputPath::off);
        unavailable.submitString("tooi ddang ");
        assertions.equal("passthrough path preserves native input",
                         unavailable.output(), "tooi ddang ");
        assertions.equal("off path has no direct backlog",
                         unavailable.metrics().queue_depth, 0);
    }

    {
        BrowserInputSession restored;
        restored.synchronize(false);
        restored.submitString("tooi ");
        restored.synchronize(true);
        assertions.truth("restored backend keeps passthrough owner",
                         restored.path() == platform::InputPath::off);
        const std::size_t before = restored.backendEventCount();
        restored.submitString("ddang gox tieengs Vieetj");
        assertions.equal("raw prefix and direct tail stay ordered",
                         restored.output(),
                         "tooi ddang gox tieengs Vieetj");
        assertions.truth("passthrough tail stayed on native event path",
                         restored.backendEventCount() > before);
    }

    {
        BrowserInputSession loss;
        loss.synchronize(true);
        loss.submitString("tooi ");
        loss.synchronize(false);
        assertions.truth("capability loss selects passthrough",
                         loss.path() == platform::InputPath::off);
        loss.submitString("ddang ");
        assertions.equal("capability loss preserves committed text",
                         loss.output(), "tôi ddang ");
        assertions.equal("capability loss leaves no direct queue",
                         loss.metrics().queue_depth, 0);
    }

    {
        BrowserInputSession boundaries;
        boundaries.synchronize(false);
        boundaries.submitString("tooi");
        boundaries.shortcutBoundary();
        assertions.equal("shortcut boundary does not commit or delete",
                         boundaries.output(), "tooi");
        assertions.truth("shortcut boundary resets policy",
                         boundaries.path() == platform::InputPath::unknown);
        boundaries.focusReset();
        assertions.equal("focus reset preserves frontend text",
                         boundaries.output(), "tooi");
    }

    {
        BrowserInputSession corpus;
        corpus.synchronize(true);
        corpus.submitString(
            "tooi tieengs dday laf booj gox tieengs Vieetj "
            "http://abc.com/a1 user@example.com "
            "std::vector<int> Console.WriteLine(\"hello\"); "
            "foo_bar->value ");
        assertions.equal(
            "direct browser corpus has exact UTF-8 output",
            corpus.output(),
            "tôi tiếng đay là bộ gõ tiếng Việt "
            "http://abc.com/a1 user@example.com "
            "std::vector<int> Console.WriteLine(\"hello\"); "
            "foo_bar->value ");
        assertions.equal("direct corpus queue drains",
                         corpus.metrics().queue_depth, 0);
    }
}

} // namespace unilume::integration::test
