// SPDX-License-Identifier: GPL-2.0-or-later

#include "preedit_fallback_controller.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <string>
#include <string_view>

namespace unilume::integration::test {
namespace {

void submitText(core::PreeditFallbackController &controller,
                std::string_view input,
                std::string &committed)
{
    for (const char key : input) {
        const core::PreeditAction action = controller.submit({
            core::KeyKind::text,
            std::string_view{&key, 1},
            false,
            false,
            false,
        });
        committed.append(action.commit_text);
    }
}

} // namespace

void runPreeditFallbackTests(Assertions &assertions)
{
    core::PreeditFallbackController telex;
    std::string committed;
    submitText(telex, "tooi", committed);
    assertions.equal("fallback keeps composition pending", committed, "");
    assertions.equal("fallback composes Telex", telex.preedit(), "tôi");

    submitText(telex, " ", committed);
    assertions.equal("fallback commits at boundary", committed, "tôi ");
    assertions.equal("fallback clears after boundary", telex.preedit(), "");

    core::PreeditFallbackController editing;
    submitText(editing, "tieengs", committed = {});
    const core::PreeditAction backspace = editing.submit({
        core::KeyKind::backspace,
        {},
        false,
        false,
        false,
    });
    assertions.truth("fallback handles composed backspace", backspace.handled);
    assertions.equal("fallback backspace result", editing.preedit(), "tiến");

    const core::PreeditAction unicode = editing.submit({
        core::KeyKind::text,
        "日本語",
        false,
        false,
        false,
    });
    assertions.equal(
        "fallback retains Unicode token",
        unicode.commit_text,
        "");
    assertions.equal(
        "fallback Unicode preedit",
        unicode.preedit_text,
        "tiến日本語");
    assertions.truth(
        "fallback Unicode output is valid UTF-8",
        isValidUtf8(unicode.preedit_text));

    core::PreeditFallbackController email;
    submitText(email, "user@example.com ", committed = {});
    assertions.equal(
        "fallback preserves email literal",
        committed,
        "user@example.com ");

    core::PreeditFallbackController code;
    submitText(
        code,
        "foo_bar->value if (x >= 10 && y != 0) npm install ",
        committed = {});
    const std::string code_visible =
        committed + std::string(code.preedit());
    assertions.equal(
        "fallback preserves code-like input",
        code_visible,
        "foo_bar->value if (x >= 10 && y != 0) npm install ");

    core::PreeditFallbackController browser;
    const std::string browser_input =
        "tooi tieengs dday laf booj gox tieengs Vieetj "
        "http://abc.com/a1 user@example.com "
        "hello.world+tag@example.org "
        "std::vector<int> Console.WriteLine(\"hello\"); "
        "foo_bar->value ";
    const std::string browser_expected =
        "tôi tiếng đay là bộ gõ tiếng Việt "
        "http://abc.com/a1 user@example.com "
        "hello.world+tag@example.org "
        "std::vector<int> Console.WriteLine(\"hello\"); "
        "foo_bar->value ";
    submitText(browser, browser_input, committed = {});
    assertions.equal(
        "Firefox-style burst corpus remains ordered",
        committed + std::string(browser.preedit()),
        browser_expected);
    browser.reset();
    submitText(browser, browser_input, committed = {});
    assertions.equal(
        "Firefox-style repeated corpus remains deterministic",
        committed + std::string(browser.preedit()),
        browser_expected);

    core::PreeditFallbackController reset;
    submitText(reset, "tooi", committed = {});
    reset.reset();
    assertions.equal("fallback reset clears preedit", reset.preedit(), "");
    submitText(reset, "aw", committed);
    assertions.equal("fallback reset isolates context", reset.preedit(), "ă");

    core::PreeditFallbackController switching;
    submitText(switching, "tooi", committed = {});
    assertions.equal("switch setup keeps Telex preedit", switching.preedit(), "tôi");
    switching.setInputMethod(UL_INPUT_METHOD_VNI);
    assertions.equal("switch clears old method composition", switching.preedit(), "");
    submitText(switching, "a1", committed = {});
    assertions.equal("switch selects VNI per context", switching.preedit(), "á");
    switching.setInputMethod(UL_INPUT_METHOD_VIQR);
    submitText(switching, "a^", committed = {});
    assertions.equal("switch selects VIQR per context", switching.preedit(), "â");
    assertions.truth("switched output remains UTF-8", isValidUtf8(switching.preedit()));

    core::PreeditFallbackController option_reload;
    submitText(option_reload, "hoas", committed = {});
    assertions.equal("option reload setup has legacy preedit",
                     option_reload.preedit(), "hóa");
    option_reload.setOptions({1, 1, 1, 1});
    assertions.equal("option reload clears old composition",
                     option_reload.preedit(), "");
    submitText(option_reload, "hoas", committed = {});
    assertions.equal("option reload applies modern tone",
                     option_reload.preedit(), "hoá");
}

} // namespace unilume::integration::test
