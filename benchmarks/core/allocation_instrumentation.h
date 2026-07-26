// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace unilume::benchmark::allocation {

enum class Operation {
    malloc_call,
    calloc_call,
    realloc_call,
    scalar_new,
    array_new,
};

struct Snapshot {
    std::uint64_t allocations{};
    std::uint64_t allocated_bytes{};
    std::uint64_t malloc_calls{};
    std::uint64_t calloc_calls{};
    std::uint64_t realloc_calls{};
    std::uint64_t scalar_new_calls{};
    std::uint64_t array_new_calls{};
};

class MeasurementScope {
public:
    MeasurementScope();
    ~MeasurementScope();

    MeasurementScope(const MeasurementScope &) = delete;
    MeasurementScope &operator=(const MeasurementScope &) = delete;

    [[nodiscard]] Snapshot finish();

private:
    bool active_{true};
};

void observeAllocation(const void *pointer, std::size_t bytes) noexcept;
void record(Operation operation, std::size_t bytes) noexcept;
[[nodiscard]] double measureEmptyScopeOverheadNanoseconds(
    std::size_t iterations = 10000);

} // namespace unilume::benchmark::allocation
