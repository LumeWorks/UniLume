// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "benchmark_types.h"

#include <chrono>
#include <string>

namespace unilume::benchmark {

class EngineFixture {
public:
    EngineFixture();
    ~EngineFixture();

    EngineFixture(const EngineFixture &) = delete;
    EngineFixture &operator=(const EngineFixture &) = delete;

    RunObservation run(const Scenario &scenario, bool measure_latency);
    AggregateObservation runAggregate(const Scenario &scenario);
    AllocationObservation runAllocations(const Scenario &scenario);
    [[nodiscard]] const std::string &output() const;

private:
    using Clock = std::chrono::steady_clock;

    void begin(const Scenario &scenario);
    void callEngine(const KeyEvent &event);
    std::uint64_t process(const KeyEvent &event);
    void applyEventOutput(const KeyEvent &event);
    void applyEngineOutput();
    void eraseUtf8Characters(int count);

    std::string output_;
};

bool isValidUtf8(std::string_view text);

} // namespace unilume::benchmark
