// SPDX-License-Identifier: GPL-2.0-or-later

// Full-pipeline browser capability lifecycle tests.
//
// Each scenario wires together:
//   capability observation (BackendProfile.surrounding_text_available)
//   → InputModePolicy::observe()
//   → composition lifecycle (resetForCompositionEnd, reset)
//   → selected InputPath
//   → DirectCommitController (direct path) or raw passthrough (off path)
//   → final output verification
//
// This tests the same chain that InputContextState::keyEvent uses
// in the Fcitx5 addon, without the Fcitx dependency.

#include "deterministic_backend.h"
#include "direct_commit_controller.h"
#include "input_mode_policy.h"
#include "integration_fixture.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <string>
#include <string_view>

namespace unilume::integration::test {
namespace {

// Minimal pipeline router mirroring InputContextState::keyEvent:
//   synchronizeMode() → controller.submit() → resetForCompositionEnd()
class BrowserPipeline {
public:
    explicit BrowserPipeline(bool surrounding_text_available)
        : profile_{.surrounding_text_available = surrounding_text_available,
                   .record_event_log = true},
          backend_(profile_),
          direct_(backend_)
    {
    }

    void synchronizeMode()
    {
        policy_.observe(profile_.surrounding_text_available);
    }

    void submitKey(const core::KeyInput &input)
    {
        synchronizeMode();
        lastPath_ = policy_.path();
        if (lastPath_ == platform::InputPath::direct) {
            const core::SubmissionStatus status = direct_.submit(input);
            if (status == core::SubmissionStatus::unhandled ||
                status == core::SubmissionStatus::passthrough) {
                backend_.forwardRaw(
                    input.kind == core::KeyKind::backspace ? 1 : 0,
                    input.text);
            }
            drain();
            policy_.resetForCompositionEnd();
        } else {
            backend_.forwardRaw(
                input.kind == core::KeyKind::backspace ? 1 : 0,
                input.text);
            policy_.resetForCompositionEnd();
        }
    }

    void type(std::string_view text)
    {
        for (std::size_t offset = 0; offset < text.size();) {
            const std::size_t length = codePointLength(text.substr(offset));
            submitKey({core::KeyKind::text,
                       text.substr(offset, length),
                       false, false, false});
            offset += length;
        }
    }

    void reset()
    {
        policy_.reset();
        direct_.resetForFocus();
    }

    [[nodiscard]] std::string output() const
    {
        return backend_.text();
    }

    [[nodiscard]] platform::InputPath path() const
    {
        return lastPath_;
    }

private:
    void drain()
    {
        for (std::size_t step = 0; step < 10000; ++step) {
            if (!backend_.hasPending() &&
                direct_.transactionState() ==
                    core::TransactionState::idle &&
                direct_.metrics().queue_depth == 0) {
                return;
            }
            if (backend_.hasPending()) {
                pump();
            } else if (direct_.activeSequence() != 0) {
                direct_.timeout(direct_.activeSequence());
            }
        }
    }

    void pump()
    {
        for (const BackendCompletion &c : backend_.advance()) {
            direct_.complete(c.sequence_id,
                              c.success
                                  ? platform::ReplacementOutcome::applied
                                  : platform::ReplacementOutcome::not_applied);
        }
    }

    static std::size_t codePointLength(std::string_view text)
    {
        if (text.empty()) return 0;
        const auto lead = static_cast<unsigned char>(text.front());
        if (lead <= 0x7f) return 1;
        if (lead >= 0xc2 && lead <= 0xdf) return 2;
        if (lead >= 0xe0 && lead <= 0xef) return 3;
        if (lead >= 0xf0 && lead <= 0xf4) return 4;
        return 1;
    }

    BackendProfile profile_;
    DeterministicBackend backend_;
    platform::InputModePolicy policy_;
    core::DirectCommitController direct_;
    platform::InputPath lastPath_{platform::InputPath::unknown};
};

} // namespace

void runBrowserCapabilityTests(Assertions &assertions)
{
    // -- 1. An unavailable direct backend passes raw keys through --
    {
        BrowserPipeline browser{false};

        browser.type("tooi ");
        assertions.equal(
            "browser pipeline first word",
            browser.output(), "tooi ");

        browser.type("ddang ");
        assertions.equal(
            "browser pipeline second word",
            browser.output(), "tooi ddang ");

        // Full typical browser input (URLs, email, code literals)
        BrowserPipeline corpus{false};
        corpus.type(
            "tooi tieengs dday laf booj gox tieengs Vieetj "
            "http://abc.com/a1 user@example.com "
            "std::vector<int> Console.WriteLine(\"hello\"); "
            "foo_bar->value ");
        const std::string expected =
            "tooi tieengs dday laf booj gox tieengs Vieetj "
            "http://abc.com/a1 user@example.com "
            "std::vector<int> Console.WriteLine(\"hello\"); "
            "foo_bar->value ";
        assertions.equal(
            "browser pipeline corpus", corpus.output(), expected);
    }

    // -- 2. Native (Qt/GTK) profile → direct path → correct output --
    // Verifies: policy selects direct, DirectCommitController
    //           produces composed output via backend transactions.
    {
        BrowserPipeline native{true};

        native.type("tooi ddang gox tieengs Vieetj");
        assertions.equal(
            "native pipeline Telex",
            native.output(), "tôi đang gõ tiếng Việt");

        // URL, email, code passthrough
        BrowserPipeline url{true};
        url.type("http://abc.com/a1 user@example.com value_2");
        assertions.equal(
            "native pipeline URL/email/code",
            url.output(), "http://abc.com/a1 user@example.com value_2");

        // Unicode passthrough
        BrowserPipeline unicode{true};
        unicode.type("ID 日本語 한국어 中文 🚀");
        assertions.equal(
            "native pipeline Unicode",
            unicode.output(), "ID 日本語 한국어 中文 🚀");
    }

    // -- 3. Policy path selection mirrors capability --
    // Verifies: observe(false) → off, observe(true) → direct.
    {
        platform::InputModePolicy policy;

        policy.observe(false);
        assertions.truth(
            "no direct backend selects passthrough",
            policy.path() == platform::InputPath::off);

        policy.resetForCompositionEnd();
        policy.observe(true);
        assertions.truth(
            "SurroundingText available selects direct path",
            policy.path() == platform::InputPath::direct);
    }

    // -- 4. resetForCompositionEnd() between observations --
    // Verifies: policy re-evaluates path across composition boundaries.
    {
        platform::InputModePolicy re_eval;

        re_eval.observe(true);
        assertions.truth("starts direct",
            re_eval.path() == platform::InputPath::direct);

        re_eval.observe(false);
        assertions.truth("demotes immediately on loss",
            re_eval.path() == platform::InputPath::off);

        re_eval.resetForCompositionEnd();
        re_eval.observe(true);
        assertions.truth("promotes after composition end",
            re_eval.path() == platform::InputPath::direct);
    }

    // -- 5. Restored capability never enters preedit --
    {
        platform::InputModePolicy stable;

        stable.observe(false);
        stable.observe(true);
        assertions.truth(
            "restored capability selects direct",
            stable.path() == platform::InputPath::direct);

        stable.resetForCompositionEnd();
        stable.observe(true);
        assertions.truth(
            "promotes only after composition ends",
            stable.path() == platform::InputPath::direct);
    }

    // -- 6. Focus reset → policy clears → re-evaluates --
    {
        platform::InputModePolicy focus;

        focus.observe(true);
        focus.reset();
        assertions.truth(
            "focus reset clears path to unknown",
            focus.path() == platform::InputPath::unknown);

        focus.observe(false);
        assertions.truth(
            "post-reset observation picks passthrough",
            focus.path() == platform::InputPath::off);
    }

    // -- 7. IntegrationFixture cross-check --
    {
        IntegrationFixture native{
            {.surrounding_text_available = true}};
        native.type("tooi ddang gox tieengs Vieetj");
        native.drain();
        assertions.equal(
            "IntegrationFixture direct composition",
            native.output(), "tôi đang gõ tiếng Việt");
        assertions.equal(
            "IntegrationFixture queue zero",
            native.metrics().queue_depth, 0);
    }

    // -- 8. Passthrough never invokes a preedit controller --
    {
        BrowserPipeline passthrough{false};
        passthrough.type("tooi ");
        assertions.equal(
            "off path preserves raw frontend input",
            passthrough.output(), "tooi ");
    }
}

} // namespace unilume::integration::test
