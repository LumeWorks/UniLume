// SPDX-License-Identifier: GPL-2.0-or-later

#include "test_assertions.h"
#include "test_suites.h"

#include "integration_fixture.h"
#include "preedit_fallback_controller.h"

#include <string>

namespace unilume::integration::test {

void runMacroTests(Assertions &assertions)
{
    macro::Snapshot macros;
    macros.enabled = true;
    macros.entries = {{"vn", "Việt Nam"}, {"sig", "xin chào"}};

    IntegrationFixture direct;
    direct.controller().setMacros(macros);
    direct.type("vn ");
    direct.drain();
    assertions.equal("macro expands through direct controller",
                     direct.output(), "Việt Nam ");

    core::PreeditFallbackController preedit;
    preedit.setMacros(macros);
    std::string committed;
    for (const char key : std::string{"sig!"}) {
        const core::PreeditAction action = preedit.submit(
            {core::KeyKind::text, std::string_view(&key, 1),
             false, false, false});
        committed.append(action.commit_text);
    }
    assertions.equal("macro punctuation remains visible in preedit",
                     preedit.preedit(), "xin chào!");
    assertions.equal("macro punctuation does not duplicate committed text",
                     committed, "");

    IntegrationFixture burst;
    burst.controller().setMacros(macros);
    for (int iteration = 0; iteration < 1000; ++iteration) {
        burst.type("vn ");
    }
    burst.drain();
    std::string expected;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        expected += "Việt Nam ";
    }
    assertions.equal("macro burst has exact output", burst.output(), expected);

    IntegrationFixture isolated;
    isolated.type("vn ");
    isolated.drain();
    assertions.equal("macro snapshots are context-local",
                     isolated.output(), "vn ");
}

} // namespace unilume::integration::test
