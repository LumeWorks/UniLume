// SPDX-License-Identifier: GPL-2.0-or-later

#include "test_assertions.h"
#include "test_suites.h"

#include "dictionary_contract.h"
#include "integration_fixture.h"
#include "preedit_fallback_controller.h"

#include <string>

namespace unilume::integration::test {

void runDictionaryTests(Assertions &assertions)
{
    const dictionary::Snapshot policy =
        dictionary::makeSnapshot(true, {"úe"}, {"as"});

    IntegrationFixture direct;
    direct.controller().setDictionary(policy);
    direct.type("as ");
    direct.drain();
    assertions.equal("dictionary restores through direct controller",
                     direct.output(), "as ");

    core::PreeditFallbackController preedit;
    preedit.setDictionary(policy);
    std::string committed;
    for (const char key : std::string{"ues "}) {
        const core::PreeditAction action =
            preedit.submit({core::KeyKind::text, std::string_view(&key, 1),
                            false, false, false});
        committed.append(action.commit_text);
    }
    assertions.equal("dictionary keep commits through preedit fallback",
                     committed, "úe ");
    assertions.equal("dictionary boundary clears fallback preedit",
                     preedit.preedit(), "");

    IntegrationFixture isolated;
    isolated.type("as ");
    isolated.drain();
    assertions.equal("dictionary policy remains context-local",
                     isolated.output(), "á ");

    IntegrationFixture soak;
    soak.controller().setDictionary(policy);
    std::string expected;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        soak.type("as ");
        expected += "as ";
    }
    soak.drain();
    assertions.equal("dictionary repeated-boundary soak is exact",
                     soak.output(), expected);
}

} // namespace unilume::integration::test
