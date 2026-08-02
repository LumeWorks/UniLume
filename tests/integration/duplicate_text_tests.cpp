// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_fixture.h"
#include "test_assertions.h"
#include "test_suites.h"

namespace unilume::integration::test {

void runDuplicateTests(Assertions &assertions)
{
    IntegrationFixture tooi{
        {.delay_events = 2, .duplicate_next_callback = true}};
    tooi.type("tooi");
    tooi.drain();
    assertions.equal("tooi does not duplicate", tooi.output(), "tôi");
    assertions.truth(
        "duplicate callback rejected",
        tooi.metrics().duplicate_prevention_count != 0);

    IntegrationFixture tieengs{
        {.delay_events = 2, .reorder_next_callback = true}};
    tieengs.type("tieengs");
    tieengs.drain();
    assertions.equal(
        "tieengs does not duplicate", tieengs.output(), "tiếng");
    assertions.truth(
        "reordered callback rejected",
        tieengs.metrics().stale_result_count != 0);

    const std::uint64_t completed =
        tieengs.metrics().completed_transactions;
    tieengs.controller().complete(
        1, platform::ReplacementOutcome::applied);
    assertions.equal(
        "stale callback cannot commit",
        tieengs.metrics().completed_transactions,
        completed);
    assertions.equal(
        "stale callback leaves output", tieengs.output(), "tiếng");
}

} // namespace unilume::integration::test
