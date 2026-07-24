// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace unilume::test {

class EngineFixture;
class TestContext;

void runTelexTests(EngineFixture &engine, TestContext &context);
void runVniTests(EngineFixture &engine, TestContext &context);
void runViqrTests(EngineFixture &engine, TestContext &context);
void runEditingTests(EngineFixture &engine, TestContext &context);
void runInputTests(EngineFixture &engine, TestContext &context);
void runUrlAndCodeInputTests(EngineFixture &engine, TestContext &context);
void runUnicodePassthroughTests(EngineFixture &engine, TestContext &context);
void runUnicodeInvalidInputTests(EngineFixture &engine, TestContext &context);
void runCompositionSpanCounterexampleTests(EngineFixture &engine,
                                           TestContext &context);

} // namespace unilume::test
