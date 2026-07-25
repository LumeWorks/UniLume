// SPDX-License-Identifier: GPL-2.0-or-later

#include "engine_fixture.h"

#include "allocation_instrumentation.h"
#include "unikey.h"

#include <stdexcept>

namespace unilume::benchmark {
namespace {

std::size_t previousUtf8Character(const std::string &text)
{
    if (text.empty()) {
        return 0;
    }
    std::size_t position = text.size() - 1;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    return position;
}

} // namespace

EngineFixture::EngineFixture()
{
    UnikeySetup();
}

EngineFixture::~EngineFixture()
{
    UnikeyCleanup();
}

RunObservation EngineFixture::run(const Scenario &scenario,
                                  bool measure_latency)
{
    begin(scenario);

    RunObservation observation;
    if (measure_latency) {
        observation.latency_ns.reserve(scenario.events.size());
    }
    for (const KeyEvent &event : scenario.events) {
        const std::uint64_t latency = process(event);
        if (measure_latency) {
            observation.latency_ns.push_back(latency);
        }
    }
    observation.output = output_;
    return observation;
}

AggregateObservation EngineFixture::runAggregate(const Scenario &scenario)
{
    begin(scenario);
    AggregateObservation observation;
    observation.events = scenario.events.size();
    for (const KeyEvent &event : scenario.events) {
        observation.total_latency_ns += process(event);
    }
    return observation;
}

AllocationObservation EngineFixture::runAllocations(const Scenario &scenario)
{
    begin(scenario);
    AllocationObservation observation;
    observation.events = scenario.events.size();
    for (const KeyEvent &event : scenario.events) {
        allocation::MeasurementScope scope;
        callEngine(event);
        const allocation::Snapshot snapshot = scope.finish();
        observation.allocations += snapshot.allocations;
        observation.allocated_bytes += snapshot.allocated_bytes;
        applyEventOutput(event);
    }
    observation.output = output_;
    return observation;
}

const std::string &EngineFixture::output() const
{
    return output_;
}

void EngineFixture::begin(const Scenario &scenario)
{
    output_.clear();
    UnikeyResetBuf();
    UnikeySetInputMethod(scenario.method);
    UnikeySetCapsState(0, 0);
}

void EngineFixture::callEngine(const KeyEvent &event)
{
    switch (event.type) {
    case EventType::key:
        UnikeyFilter(event.key);
        break;
    case EventType::backspace:
        UnikeyBackspacePress();
        break;
    case EventType::reset:
        UnikeyResetBuf();
        break;
    }
}

std::uint64_t EngineFixture::process(const KeyEvent &event)
{
    const auto start = Clock::now();
    callEngine(event);
    const auto end = Clock::now();
    applyEventOutput(event);
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count());
}

void EngineFixture::applyEventOutput(const KeyEvent &event)
{
    if (event.type == EventType::key && UnikeyBackspaces == 0 &&
        UnikeyBufChars == 0) {
        output_.push_back(static_cast<char>(event.key));
    } else if (event.type != EventType::reset) {
        applyEngineOutput();
    }
}

void EngineFixture::applyEngineOutput()
{
    eraseUtf8Characters(UnikeyBackspaces);
    output_.append(reinterpret_cast<const char *>(UnikeyBuf),
                   static_cast<std::size_t>(UnikeyBufChars));
}

void EngineFixture::eraseUtf8Characters(int count)
{
    while (count-- > 0 && !output_.empty()) {
        output_.erase(previousUtf8Character(output_));
    }
}

bool isValidUtf8(std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t continuation = 0;
        if (lead <= 0x7f) {
            continuation = 0;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            continuation = 1;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            continuation = 2;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            continuation = 3;
        } else {
            return false;
        }
        if (index + continuation >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuation; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xc0) !=
                0x80) {
                return false;
            }
        }
        const auto second = continuation == 0
                                ? 0
                                : static_cast<unsigned char>(text[index + 1]);
        if ((lead == 0xe0 && second < 0xa0) ||
            (lead == 0xed && second > 0x9f) ||
            (lead == 0xf0 && second < 0x90) ||
            (lead == 0xf4 && second > 0x8f)) {
            return false;
        }
        index += continuation + 1;
    }
    return true;
}

} // namespace unilume::benchmark
