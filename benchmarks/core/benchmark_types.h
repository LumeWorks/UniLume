// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "unikey.h"

#include <cstdint>
#include <string>
#include <vector>

namespace unilume::benchmark {

enum class EventType {
    key,
    backspace,
    reset,
};

struct KeyEvent {
    EventType type{EventType::key};
    unsigned char key{};
};

struct Scenario {
    std::string name;
    UkInputMethod method{UkTelex};
    std::vector<KeyEvent> events;
    std::string expected_output;
};

struct Corpus {
    std::string name;
    std::vector<Scenario> scenarios;
};

struct RunObservation {
    std::string output;
    std::vector<std::uint64_t> latency_ns;
};

struct AggregateObservation {
    std::uint64_t events{};
    std::uint64_t total_latency_ns{};
};

struct AllocationObservation {
    std::string output;
    std::uint64_t events{};
    std::uint64_t allocations{};
    std::uint64_t allocated_bytes{};
};

} // namespace unilume::benchmark
