// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace unilume::benchmark::allocation {

struct Snapshot {
    std::uint64_t allocations{};
    std::uint64_t allocated_bytes{};
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

void record(std::size_t bytes) noexcept;
[[nodiscard]] double measureEmptyScopeOverheadNanoseconds(
    std::size_t iterations = 10000);

} // namespace unilume::benchmark::allocation
