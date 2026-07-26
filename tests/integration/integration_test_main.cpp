// SPDX-License-Identifier: GPL-2.0-or-later

#include "test_suites.h"

#include <iostream>
#include <string_view>

int main(int argc, char **argv)
{
    using namespace unilume::integration::test;

    Assertions assertions;

    if (argc < 2 || std::string_view(argv[1]) == "all") {
        runImmediateTests(assertions);
        runDelayedTests(assertions);
        runDuplicateTests(assertions);
        runTransactionTests(assertions);
        runPreeditFallbackTests(assertions);
        runBrowserCapabilityTests(assertions);
        runBrowserInputSessionTests(assertions);
        runInputModePolicyTests(assertions);
        runOptionsTests(assertions);
        runMacroTests(assertions);
        runDictionaryTests(assertions);
        runBurstTests(assertions);
        runSoakSmokeTests(assertions);
        runZeroPreeditArchitectureTests(assertions);
        runZeroPreeditSoakTests(assertions);
    } else {
        const std::string_view suite{argv[1]};
        if (suite == "immediate") {
            runImmediateTests(assertions);
        } else if (suite == "delayed") {
            runDelayedTests(assertions);
        } else if (suite == "duplicate") {
            runDuplicateTests(assertions);
        } else if (suite == "transaction") {
            runTransactionTests(assertions);
        } else if (suite == "preedit-fallback") {
            runPreeditFallbackTests(assertions);
        } else if (suite == "browser-capability") {
            runBrowserCapabilityTests(assertions);
        } else if (suite == "browser-input-session") {
            runBrowserInputSessionTests(assertions);
        } else if (suite == "mode-policy") {
            runInputModePolicyTests(assertions);
        } else if (suite == "options") {
            runOptionsTests(assertions);
        } else if (suite == "macro") {
            runMacroTests(assertions);
        } else if (suite == "dictionary") {
            runDictionaryTests(assertions);
        } else if (suite == "burst") {
            runBurstTests(assertions);
        } else if (suite == "soak-smoke") {
            runSoakSmokeTests(assertions);
        } else if (suite == "zero-preedit-architecture") {
            runZeroPreeditArchitectureTests(assertions);
        } else if (suite == "zero-preedit-soak") {
            runZeroPreeditSoakTests(assertions);
        } else {
            std::cerr << "unknown integration suite: " << suite << '\n';
            return 2;
        }
    }

    if (assertions.failures() != 0) {
        std::cerr << assertions.failures() << " integration assertion(s) failed\n";
        return 1;
    }
    return 0;
}
