// SPDX-License-Identifier: GPL-2.0-or-later

#include "allocation_instrumentation.h"

#include <chrono>
#include <cstdlib>
#include <limits>
#include <new>

namespace unilume::benchmark::allocation {
namespace {

thread_local bool measurement_active = false;
thread_local bool recording = false;
thread_local Snapshot counters;

} // namespace

MeasurementScope::MeasurementScope()
{
    counters = {};
    measurement_active = true;
}

MeasurementScope::~MeasurementScope()
{
    if (active_) {
        measurement_active = false;
    }
}

Snapshot MeasurementScope::finish()
{
    measurement_active = false;
    active_ = false;
    return counters;
}

void record(std::size_t bytes) noexcept
{
    if (!measurement_active || recording) {
        return;
    }
    recording = true;
    ++counters.allocations;
    counters.allocated_bytes += static_cast<std::uint64_t>(bytes);
    recording = false;
}

double measureEmptyScopeOverheadNanoseconds(std::size_t iterations)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        MeasurementScope scope;
        const Snapshot snapshot = scope.finish();
        if (snapshot.allocations != 0 || snapshot.allocated_bytes != 0) {
            return -1.0;
        }
    }
    const auto end = Clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    return static_cast<double>(elapsed) / static_cast<double>(iterations);
}

} // namespace unilume::benchmark::allocation

extern "C" {
void *__real_malloc(std::size_t size);
void *__real_calloc(std::size_t count, std::size_t size);
void *__real_realloc(void *pointer, std::size_t size);
void __real_free(void *pointer);

void *__wrap_malloc(std::size_t size)
{
    void *pointer = __real_malloc(size);
    if (pointer != nullptr) {
        unilume::benchmark::allocation::record(size);
    }
    return pointer;
}

void *__wrap_calloc(std::size_t count, std::size_t size)
{
    void *pointer = __real_calloc(count, size);
    if (pointer != nullptr &&
        (size == 0 || count <= std::numeric_limits<std::size_t>::max() / size)) {
        unilume::benchmark::allocation::record(count * size);
    }
    return pointer;
}

void *__wrap_realloc(void *original, std::size_t size)
{
    void *pointer = __real_realloc(original, size);
    if (pointer != nullptr && size != 0) {
        unilume::benchmark::allocation::record(size);
    }
    return pointer;
}

void __wrap_free(void *pointer)
{
    __real_free(pointer);
}
}

void *operator new(std::size_t size)
{
    void *pointer = __real_malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    unilume::benchmark::allocation::record(size);
    return pointer;
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    void *pointer = __real_malloc(size == 0 ? 1 : size);
    if (pointer != nullptr) {
        unilume::benchmark::allocation::record(size);
    }
    return pointer;
}

void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void *pointer) noexcept
{
    __real_free(pointer);
}

void operator delete[](void *pointer) noexcept
{
    __real_free(pointer);
}

void operator delete(void *pointer, std::size_t) noexcept
{
    __real_free(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept
{
    __real_free(pointer);
}

void operator delete(void *pointer, const std::nothrow_t &) noexcept
{
    __real_free(pointer);
}

void operator delete[](void *pointer, const std::nothrow_t &) noexcept
{
    __real_free(pointer);
}
