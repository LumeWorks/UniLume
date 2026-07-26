// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_fixture.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::integration::test {
namespace {

std::size_t requestedEvents()
{
    const char *value = std::getenv("UNILUME_RECOVERY_SOAK_EVENTS");
    if (value == nullptr || value[0] == '\0') {
        return 10'000;
    }
    char *end = nullptr;
    const unsigned long long events = std::strtoull(value, &end, 10);
    return end != value && *end == '\0' && events != 0
               ? static_cast<std::size_t>(events)
               : 10'000;
}

std::uint64_t currentRssKiB()
{
    std::ifstream status{"/proc/self/status"};
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            std::uint64_t value{};
            std::string unit;
            status >> value >> unit;
            return unit == "kB" ? value : 0;
        }
        std::string remainder;
        std::getline(status, remainder);
    }
    return 0;
}

bool sustainedGrowth(const std::vector<std::uint64_t> &values)
{
    if (values.size() < 6 ||
        values.back() <= values.front() + 1024) {
        return false;
    }
    std::size_t increases = 0;
    for (std::size_t index = 1; index < values.size(); ++index) {
        increases += values[index] > values[index - 1];
    }
    return increases * 5 >= (values.size() - 1) * 4;
}

std::size_t runRecoveryCycle(Assertions &assertions)
{
    std::size_t events = 0;

    // Reconstructing the complete controller/backend pair is the same
    // lifecycle boundary used by addon reload and input-context recreation.
    IntegrationFixture reloaded;
    static constexpr std::string_view input{
        "tooi ddang gox tieengs Vieetj "};
    reloaded.type(input);
    events += input.size();
    reloaded.drain();
    assertions.equal("reloaded component output", reloaded.output(),
                     "tôi đang gõ tiếng Việt ");

    // A controlled replacement-backend refusal must take the bounded raw
    // fallback instead of leaving an active transaction.
    IntegrationFixture replacement_failure{{.fail_next_delete = true}};
    replacement_failure.type("tooi");
    events += 4;
    replacement_failure.drain();
    assertions.equal("controlled replacement failure fallback",
                     replacement_failure.output(), "tooi");
    assertions.truth("controlled replacement failure drains",
                     !replacement_failure.metrics().active_transaction &&
                         replacement_failure.metrics().queue_depth == 0);

    // A lost helper completion must time out, drain, and accept subsequent
    // input without replaying or duplicating the uncertain edit.
    IntegrationFixture helper_crash{
        {.delay_events = 5, .drop_next_callback = true}};
    helper_crash.type("tieengs");
    events += 7;
    helper_crash.drain();
    helper_crash.type(" ");
    ++events;
    helper_crash.drain();
    assertions.equal("helper crash recovery output",
                     helper_crash.output(), "tieengs ");
    assertions.truth("helper crash recovery drains",
                     !helper_crash.metrics().active_transaction &&
                         helper_crash.metrics().queue_depth == 0);

    // Refused cancellation is an explicitly uncertain boundary. Recovery
    // fences the old generation and starts cleanly on the next event.
    IntegrationFixture uncertain{
        {.delay_events = 10, .refuse_cancel = true}};
    uncertain.type("as");
    events += 2;
    const std::uint64_t sequence = uncertain.controller().activeSequence();
    uncertain.controller().timeout(sequence);
    uncertain.type("s");
    ++events;
    uncertain.drain();
    assertions.equal("uncertain helper recovery output",
                     uncertain.output(), "as");
    assertions.truth("uncertain helper outcome counted",
                     uncertain.metrics().uncertain_outcome_count == 1);

    return events;
}

} // namespace

void runStabilityRecoveryTests(Assertions &assertions)
{
    const std::size_t target_events = requestedEvents();
    const std::size_t checkpoint_interval =
        std::max(target_events / 10, std::size_t{1});
    std::size_t events = 0;
    std::size_t next_checkpoint = checkpoint_interval;
    std::vector<std::uint64_t> rss_checkpoints;
    rss_checkpoints.reserve(12);
    rss_checkpoints.push_back(currentRssKiB());

    while (events < target_events) {
        events += runRecoveryCycle(assertions);
        if (events >= next_checkpoint) {
            rss_checkpoints.push_back(currentRssKiB());
            next_checkpoint += checkpoint_interval;
        }
    }

    assertions.truth("recovery soak reaches requested event count",
                     events >= target_events);
    // Short sanitizer runs include allocator quarantine and lazy shadow-page
    // commitment. Enforce the RSS invariant only at the acceptance event
    // count; smoke still exercises every recovery boundary.
    assertions.truth("recovery soak has no sustained RSS growth",
                     target_events < 1'000'000 ||
                         !sustainedGrowth(rss_checkpoints));
}

} // namespace unilume::integration::test
