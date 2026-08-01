// SPDX-License-Identifier: GPL-2.0-or-later

#include "test_suites.h"
#include "test_support/test_context.h"

#include <array>

namespace unilume::test {

void runTelexTests(EngineFixture &engine, TestContext &context)
{
    static constexpr std::array vectors{
        RegressionVector{"Telex basic phrase", UkTelex, "tieengs Vieetj", "tiếng Việt"},
        RegressionVector{"Telex acute", UkTelex, "as", "á"},
        RegressionVector{"Telex grave", UkTelex, "af", "à"},
        RegressionVector{"Telex hook above", UkTelex, "ar", "ả"},
        RegressionVector{"Telex tilde", UkTelex, "ax", "ã"},
        RegressionVector{"Telex dot below", UkTelex, "aj", "ạ"},
        RegressionVector{"Telex breve", UkTelex, "aw", "ă"},
        RegressionVector{"Telex a circumflex", UkTelex, "aa", "â"},
        RegressionVector{"Telex e circumflex", UkTelex, "ee", "ê"},
        RegressionVector{"Telex o circumflex", UkTelex, "oo", "ô"},
        RegressionVector{"Telex o horn", UkTelex, "ow", "ơ"},
        RegressionVector{"Telex u horn", UkTelex, "uw", "ư"},
        RegressionVector{"Telex d stroke", UkTelex, "dd", "đ"},
        RegressionVector{"Telex d-stroke shorthand survives boundary", UkTelex, "ddm ", "đm "},
        RegressionVector{"Telex d-stroke multi-consonant slang", UkTelex, "ddcm ", "đcm "},
        RegressionVector{"Telex rapid multi-stage tone replacement", UkTelex,
                         "chaamj vows ", "chậm vớ "},
        RegressionVector{"Telex rapid horn and tone replacement", UkTelex,
                         "chaamj vowis ", "chậm với "},
        RegressionVector{"Telex uppercase d-stroke shorthand", UkTelex, "DDm ", "Đm "},
        RegressionVector{"Telex repeated a modifier", UkTelex, "aaa", "aa"},
        RegressionVector{"Telex repeated breve modifier", UkTelex, "aww", "aw"},
        RegressionVector{"Telex repeated e modifier", UkTelex, "eee", "ee"},
        RegressionVector{"Telex repeated o modifier", UkTelex, "ooo", "oo"},
        RegressionVector{"Telex repeated o horn", UkTelex, "oww", "ow"},
        RegressionVector{"Telex repeated u horn", UkTelex, "uww", "uw"},
        RegressionVector{"Telex repeated d modifier", UkTelex, "ddd", "dd"},
        RegressionVector{"Telex remove tone", UkTelex, "asz", "a"},
        RegressionVector{"Telex replace tone", UkTelex, "asf", "à"},
        RegressionVector{"Telex replace vowel modifier", UkTelex, "aaw", "ă"},
        RegressionVector{"Telex uppercase", UkTelex, "AS AW AA EE OO OW UW DD", "Á Ă Â Ê Ô Ơ Ư Đ"},
        RegressionVector{"Telex tone position oa", UkTelex, "hoas", "hóa"},
        RegressionVector{"Telex tone position oe", UkTelex, "khoer", "khỏe"},
        RegressionVector{"Telex tone position uy", UkTelex, "thuyr", "thủy"},
        RegressionVector{"Telex three-vowel syllable", UkTelex, "quyeens", "quyến"},
    };
    context.runVectors(engine, vectors);
}

} // namespace unilume::test
