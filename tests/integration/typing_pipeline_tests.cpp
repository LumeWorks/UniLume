// SPDX-License-Identifier: GPL-2.0-or-later

#include "preedit_fallback_controller.h"
#include "integration_fixture.h"
#include "test_assertions.h"
#include "test_suites.h"
#include "typing_pipeline.h"

#include <string>
#include <string_view>

namespace unilume::integration::test {
namespace {

std::size_t previousCharacter(std::string_view text, std::size_t position)
{
    --position;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    return position;
}

std::string compose(
    UlInputMethod method,
    const core::TypingConvenienceOptions &options,
    std::string_view keys)
{
    core::TypingPipeline pipeline(method);
    pipeline.setTypingOptions(options);
    std::string output;
    for (const char key : keys) {
        const core::KeyResult result = pipeline.process({
            core::KeyKind::text,
            std::string_view{&key, 1},
            false,
            false,
            false,
        });
        if (!result.handled) {
            output.push_back(key);
            continue;
        }
        for (std::int32_t count = 0;
             count < result.delete_before_cursor; ++count) {
            if (output.empty()) {
                return {};
            }
            output.erase(previousCharacter(output, output.size()));
        }
        output.append(result.commit_text);
    }
    return output;
}

std::string composeWithLineBreak(
    const core::TypingConvenienceOptions &options,
    std::string_view before,
    std::string_view after)
{
    core::TypingPipeline pipeline;
    pipeline.setTypingOptions(options);
    std::string output;
    const auto append = [&](std::string_view keys) {
        for (const char key : keys) {
            const core::KeyResult result = pipeline.process({
                core::KeyKind::text,
                std::string_view{&key, 1},
                false,
                false,
                false,
            });
            for (std::int32_t count = 0;
                 count < result.delete_before_cursor; ++count) {
                output.erase(previousCharacter(output, output.size()));
            }
            output.append(result.commit_text);
        }
    };
    append(before);
    pipeline.lineBreak();
    output.push_back('\n');
    append(after);
    return output;
}

std::string composePreedit(
    const core::TypingConvenienceOptions &options,
    std::string_view keys)
{
    core::PreeditFallbackController controller;
    controller.setTypingOptions(options);
    std::string output;
    for (const char key : keys) {
        const core::PreeditAction action = controller.submit({
            core::KeyKind::text,
            std::string_view{&key, 1},
            false,
            false,
            false,
        });
        output.append(action.commit_text);
    }
    output.append(controller.preedit());
    return output;
}

} // namespace

void runTypingPipelineTests(Assertions &assertions)
{
    const core::TypingConvenienceOptions inherited;
    assertions.equal(
        "inactive pipeline preserves the full compatibility corpus",
        compose(
            UL_INPUT_METHOD_TELEX,
            inherited,
            "tooi tieengs http://abc.com/a1 user@example.com "
            "foo_bar->value --flag "),
        "tôi tiếng http://abc.com/a1 user@example.com "
        "foo_bar->value --flag ");

    core::TypingConvenienceOptions prose;
    prose.auto_capitalize = true;
    prose.double_space_to_period = true;
    prose.double_hyphen_to_em_dash = true;
    assertions.equal(
        "ordered prose transforms compose deterministically",
        compose(
            UL_INPUT_METHOD_TELEX,
            prose,
            "xin  chao. tieengs-- Vieetj! ban? toi"),
        "xin. Chao. Tiếng— Việt! Ban? Toi");
    assertions.equal(
        "line break arms capitalization without consuming Enter",
        composeWithLineBreak(prose, "xin", "chao"),
        "xin\nChao");
    assertions.equal(
        "indentation and command flags are never prose replacements",
        compose(
            UL_INPUT_METHOD_TELEX,
            prose,
            "  --flag foo_bar--value "),
        "  --flag foo_bar--value ");
    assertions.equal(
        "URL email and code literal contexts ignore conveniences",
        compose(
            UL_INPUT_METHOD_TELEX,
            prose,
            "http://a--b.example/x  user--tag@example.com  "
            "foo_bar--value  "),
        "http://a--b.example/x  user--tag@example.com  "
        "foo_bar--value  ");

    core::TypingConvenienceOptions shortcuts;
    shortcuts.w_shortcut = core::ShortcutScope::everywhere;
    shortcuts.bracket_shortcut = core::ShortcutScope::everywhere;
    assertions.equal(
        "Telex inherited shortcuts remain composable",
        compose(UL_INPUT_METHOD_TELEX, shortcuts, "ws [s ]s"),
        "ứ ớ ứ");
    assertions.equal(
        "VNI shortcuts feed the VNI engine and accept later tones",
        compose(UL_INPUT_METHOD_VNI, shortcuts, "w1 [1 ]1"),
        "ứ ớ ứ");
    assertions.equal(
        "VIQR shortcuts feed the VIQR engine and accept later tones",
        compose(UL_INPUT_METHOD_VIQR, shortcuts, "w' [' ]'"),
        "ứ ớ ứ");

    shortcuts.w_shortcut = core::ShortcutScope::non_start;
    shortcuts.bracket_shortcut = core::ShortcutScope::non_start;
    assertions.equal(
        "non-start scope keeps leading shortcut keys literal",
        compose(UL_INPUT_METHOD_TELEX, shortcuts, "w [ tw t["),
        "w [ tư tơ");
    shortcuts.w_shortcut = core::ShortcutScope::disabled;
    shortcuts.bracket_shortcut = core::ShortcutScope::disabled;
    assertions.equal(
        "disabled scope bypasses standalone shortcut transforms",
        compose(UL_INPUT_METHOD_TELEX, shortcuts, "w [ tw t["),
        "w [ tw t[");

    assertions.equal(
        "preedit path applies double-space as one visible edit",
        composePreedit(prose, "xin  chao"),
        "xin. Chao");
    assertions.equal(
        "preedit path commits deferred boundary before ordinary input",
        composePreedit(prose, "xin chao"),
        "xin chao");
    assertions.equal(
        "preedit path applies the bounded em-dash edit",
        composePreedit(prose, "xin-- chao"),
        "xin— chao");

    IntegrationFixture direct;
    direct.controller().setTypingOptions(prose);
    direct.type("xin  chao");
    direct.drain();
    assertions.equal(
        "direct path uses the same ordered pipeline contract",
        direct.output(),
        "xin. Chao");

    core::TypingPipeline reset_pipeline;
    reset_pipeline.setTypingOptions(prose);
    static_cast<void>(reset_pipeline.process(
        {core::KeyKind::text, ".", false, false, false}));
    static_cast<void>(reset_pipeline.process(
        {core::KeyKind::text, " ", false, false, false}));
    reset_pipeline.reset();
    const core::KeyResult after_reset = reset_pipeline.process(
        {core::KeyKind::text, "a", false, false, false});
    assertions.equal(
        "focus reset clears capitalization state",
        after_reset.commit_text,
        "a");

    core::TypingPipeline shortcut_reset;
    shortcut_reset.setTypingOptions(prose);
    static_cast<void>(shortcut_reset.process(
        {core::KeyKind::text, ".", false, false, false}));
    static_cast<void>(shortcut_reset.process(
        {core::KeyKind::text, " ", false, false, false}));
    static_cast<void>(shortcut_reset.process(
        {core::KeyKind::text, "x", false, false, true}));
    const core::KeyResult after_shortcut = shortcut_reset.process(
        {core::KeyKind::text, "a", false, false, false});
    assertions.equal(
        "control shortcut clears transform state and is never rewritten",
        after_shortcut.commit_text,
        "a");
}

} // namespace unilume::integration::test
