// SPDX-License-Identifier: GPL-2.0-or-later

#include "test_suites.h"
#include "test_support/engine_fixture.h"
#include "test_support/test_context.h"

#include <cstdlib>
#include <iostream>

int main()
{
    unilume::test::EngineFixture engine;
    unilume::test::TestContext context;

    unilume::test::runTelexTests(engine, context);
    unilume::test::runVniTests(engine, context);
    unilume::test::runViqrTests(engine, context);
    unilume::test::runEditingTests(engine, context);
    unilume::test::runInputTests(engine, context);
    unilume::test::runUrlAndCodeInputTests(engine, context);
    unilume::test::runUnicodePassthroughTests(engine, context);
    unilume::test::runUnicodeInvalidInputTests(engine, context);
    unilume::test::runCompositionSpanCounterexampleTests(engine, context);

    if (context.failures() != 0) {
        std::cerr << context.failures() << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All engine tests passed\n";
    return EXIT_SUCCESS;
}
