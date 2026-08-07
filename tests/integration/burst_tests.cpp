// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_fixture.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <algorithm>
#include <array>
#include <string>

namespace unilume::integration::test {
namespace {

std::string burstInput(std::size_t keys)
{
    static constexpr std::string_view corpus{
        "tieengs Vieetj http://abc.com/a1 user@example.com value_2 日本語 "};
    std::string input;
    input.reserve(keys);
    while (input.size() < keys) {
        input.append(corpus.substr(
            0, std::min(corpus.size(), keys - input.size())));
    }
    return input;
}

std::string referenceOutput(std::string_view input)
{
    IntegrationFixture reference;
    reference.type(input);
    reference.drain();
    return reference.output();
}

} // namespace

void runBurstTests(Assertions &assertions)
{
    static constexpr std::string_view reported_raw{
        "xin chaof cacs baf con, ddaay la tin nhanws dduocwj gox tuwf "
        "UniLume"};
    static constexpr std::string_view reported_expected{
        "xin chào các bà con, đây la tin nhắn được gõ từ UniLume"};
    for (const std::size_t virtual_delay : {0U, 1U, 10U}) {
        IntegrationFixture corpus{{.delay_events = virtual_delay}};
        corpus.type(reported_raw);
        corpus.drain();
        assertions.equal(
            "reported Telex corpus is exact at every pace",
            corpus.output(), reported_expected);
        assertions.equal(
            "reported Telex corpus leaves no direct backlog",
            corpus.metrics().queue_depth, 0);
        assertions.equal(
            "reported Telex corpus has no failed transaction",
            corpus.metrics().aborted_transactions, 0);
    }

    static constexpr std::array<std::size_t, 5> sizes{
        10, 50, 100, 1000, 10000};
    for (const std::size_t size : sizes) {
        const std::string input = burstInput(size);
        const std::string expected = referenceOutput(input);
        IntegrationFixture fixture{{.delay_events = 5}};
        fixture.type(input);
        fixture.drain();
        assertions.equal("delayed burst output", fixture.output(), expected);
        assertions.truth("burst UTF-8", isValidUtf8(fixture.output()));
        assertions.truth(
            "burst queue bounded",
            fixture.metrics().max_queue_depth <=
                core::DirectCommitController::queue_capacity);
        assertions.equal(
            "burst final queue", fixture.metrics().queue_depth, 0);
    }

    const std::string deep_input = burstInput(400);
    const std::string deep_expected = referenceOutput(deep_input);
    IntegrationFixture deep_fixture{{.delay_events = 400}};
    deep_fixture.type(deep_input);
    deep_fixture.drain();
    assertions.equal(
        "deep acknowledged burst output",
        deep_fixture.output(),
        deep_expected);
    assertions.equal(
        "deep acknowledged burst overflow",
        deep_fixture.metrics().queue_overflow_count,
        0);
    assertions.truth(
        "deep acknowledged burst bounded",
        deep_fixture.metrics().max_queue_depth <=
            core::DirectCommitController::queue_capacity);
}

void runSoakSmokeTests(Assertions &assertions)
{
    const std::string input = burstInput(10000);
    const std::string expected = referenceOutput(input);
    // Five virtual events is the sustained delayed profile. Longer 10/50
    // event delays are covered by bounded bursts, where backlog is expected
    // but cannot grow without limit.
    IntegrationFixture fixture{{.delay_events = 5}};
    fixture.type(input);
    fixture.drain();
    assertions.equal(
        "soak smoke aborted transactions",
        fixture.metrics().aborted_transactions,
        0);
    assertions.equal(
        "soak smoke queue overflow",
        fixture.metrics().queue_overflow_count,
        0);
    assertions.equal(
        "soak smoke stale results",
        fixture.metrics().stale_result_count,
        0);
    assertions.equal("soak smoke output", fixture.output(), expected);
    assertions.equal(
        "soak smoke final queue", fixture.metrics().queue_depth, 0);
    assertions.truth(
        "soak smoke final transaction",
        !fixture.metrics().active_transaction);
}

} // namespace unilume::integration::test
