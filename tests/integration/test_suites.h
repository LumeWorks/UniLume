// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "test_assertions.h"

namespace unilume::integration::test {

void runImmediateTests(Assertions &assertions);
void runDelayedTests(Assertions &assertions);
void runDuplicateTests(Assertions &assertions);
void runTransactionTests(Assertions &assertions);
void runPreeditFallbackTests(Assertions &assertions);
void runInputModePolicyTests(Assertions &assertions);
void runOptionsTests(Assertions &assertions);
void runMacroTests(Assertions &assertions);
void runDictionaryTests(Assertions &assertions);
void runBrowserCapabilityTests(Assertions &assertions);
void runBrowserInputSessionTests(Assertions &assertions);
void runBurstTests(Assertions &assertions);
void runSoakSmokeTests(Assertions &assertions);
void runZeroPreeditArchitectureTests(Assertions &assertions);
void runZeroPreeditSoakTests(Assertions &assertions);

} // namespace unilume::integration::test
