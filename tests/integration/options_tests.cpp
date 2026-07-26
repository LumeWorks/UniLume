// SPDX-License-Identifier: GPL-2.0-or-later

#include "test_assertions.h"
#include "test_suites.h"

#include "engine_options.h"
#include "engine_context.h"
#include "unilume_context.h"

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

std::string compose(const UlEngineOptions &options, std::string_view keys)
{
    core::EngineContext engine;
    engine.setOptions(options);
    std::string output;
    for (const char key : keys) {
        const core::KeyInput input{core::KeyKind::text,
                                   std::string_view(&key, 1), false, false,
                                   false};
        const core::KeyResult result = engine.process(input);
        if (!result.handled) {
            output.push_back(key);
            continue;
        }
        for (std::int32_t count = 0;
             count < result.delete_before_cursor && !output.empty(); ++count) {
            output.erase(previousCharacter(output, output.size()));
        }
        output.append(result.commit_text);
    }
    return output;
}

} // namespace

void runOptionsTests(Assertions &assertions)
{
    config::Snapshot snapshot = config::defaults();
    snapshot.spell_check = false;
    snapshot.free_marking = false;
    snapshot.modern_tone = true;
    snapshot.auto_restore = false;
    const UlEngineOptions mapped = core::engineOptionsFromSnapshot(snapshot);
    assertions.truth("snapshot maps every real UniKey option",
                     mapped.spell_check == 0 && mapped.free_marking == 0 &&
                         mapped.modern_tone == 1 && mapped.auto_restore == 0);

    UlEngineContext *first{};
    UlEngineContext *second{};
    assertions.truth("options create first context",
                     ul_engine_create(UL_INPUT_METHOD_TELEX, &first) == UL_STATUS_OK);
    assertions.truth("options create second context",
                     ul_engine_create(UL_INPUT_METHOD_TELEX, &second) == UL_STATUS_OK);
    if (!first || !second) {
        ul_engine_destroy(first);
        ul_engine_destroy(second);
        return;
    }

    UlEngineOptions defaults{};
    assertions.truth("options read defaults",
                     ul_engine_get_options(first, &defaults) == UL_STATUS_OK);
    assertions.truth("default spell checking enabled", defaults.spell_check == 1);
    assertions.truth("default free marking enabled", defaults.free_marking == 1);
    assertions.truth("default modern tone disabled", defaults.modern_tone == 0);
    assertions.truth("default auto restore enabled", defaults.auto_restore == 1);

    const UlEngineOptions changed{0, 0, 1, 0};
    assertions.truth("options apply valid snapshot",
                     ul_engine_set_options(first, &changed) == UL_STATUS_OK);
    UlEngineOptions observed{};
    assertions.truth("options read changed snapshot",
                     ul_engine_get_options(first, &observed) == UL_STATUS_OK);
    assertions.truth("options retain every changed field",
                     observed.spell_check == 0 && observed.free_marking == 0 &&
                         observed.modern_tone == 1 && observed.auto_restore == 0);

    const UlEngineOptions invalid{2, 0, 1, 0};
    assertions.truth("options reject non-boolean input",
                     ul_engine_set_options(first, &invalid) == UL_STATUS_INVALID_ARGUMENT);
    assertions.truth("invalid options preserve active snapshot",
                     ul_engine_get_options(first, &observed) == UL_STATUS_OK &&
                         observed.spell_check == 0 && observed.free_marking == 0);
    assertions.truth("options remain context-local",
                     ul_engine_get_options(second, &observed) == UL_STATUS_OK &&
                         observed.spell_check == 1 && observed.free_marking == 1);

    const UlEngineOptions legacy_tone{1, 1, 0, 1};
    const UlEngineOptions modern_tone{1, 1, 1, 1};
    assertions.equal("legacy tone placement remains default",
                     compose(legacy_tone, "hoas "), "hóa ");
    assertions.equal("modern tone placement changes oa",
                     compose(modern_tone, "hoas "), "hoá ");

    const UlEngineOptions free_marking{1, 1, 0, 1};
    const UlEngineOptions constrained_marking{1, 0, 0, 1};
    assertions.equal("free marking rewrites earlier consonant",
                     compose(free_marking, "dad"), "đa");
    assertions.equal("constrained marking preserves rejected key",
                     compose(constrained_marking, "dad"), "dad");

    const UlEngineOptions spell_check{1, 0, 0, 0};
    const UlEngineOptions no_spell_check{0, 0, 0, 0};
    assertions.equal("spell check protects non-Vietnamese token",
                     compose(spell_check, "ues "), "ues ");
    assertions.equal("disabled spell check permits marking",
                     compose(no_spell_check, "ues "), "úe ");

    const UlEngineOptions auto_restore{1, 1, 0, 1};
    const UlEngineOptions no_auto_restore{1, 1, 0, 0};
    assertions.equal("auto restore returns raw non-Vietnamese keys",
                     compose(auto_restore, "wikipedia "), "wikipedia ");
    assertions.equal("disabled auto restore retains composition",
                     compose(no_auto_restore, "wikipedia "), "ưikipedia ");

    ul_engine_destroy(first);
    ul_engine_destroy(second);
}

} // namespace unilume::integration::test
