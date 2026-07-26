// SPDX-License-Identifier: GPL-2.0-or-later

#include "allocation_instrumentation.h"

#include <cstdlib>
#include <iostream>
#include <new>

namespace {

bool expect(const char *name,
            const unilume::benchmark::allocation::Snapshot &actual,
            unilume::benchmark::allocation::Operation operation,
            std::uint64_t bytes)
{
    const auto operationCalls = [&actual, operation] {
        using unilume::benchmark::allocation::Operation;
        switch (operation) {
        case Operation::malloc_call:
            return actual.malloc_calls;
        case Operation::calloc_call:
            return actual.calloc_calls;
        case Operation::realloc_call:
            return actual.realloc_calls;
        case Operation::scalar_new:
            return actual.scalar_new_calls;
        case Operation::array_new:
            return actual.array_new_calls;
        }
        return std::uint64_t{0};
    }();
    if (actual.allocations == 1 && actual.allocated_bytes == bytes &&
        operationCalls == 1) {
        return true;
    }
    std::cerr << name << ": expected one recorded operation and bytes="
              << bytes << " but got allocations="
              << actual.allocations << " bytes=" << actual.allocated_bytes
              << " matching-operation-calls=" << operationCalls
              << '\n';
    return false;
}

} // namespace

int main()
{
    using unilume::benchmark::allocation::MeasurementScope;
    using unilume::benchmark::allocation::Snapshot;

    bool ok = true;

    void *c_malloc = nullptr;
    {
        MeasurementScope measured;
        c_malloc = std::malloc(17);
        unilume::benchmark::allocation::observeAllocation(c_malloc, 17);
        ok = expect("malloc fixture", measured.finish(),
                    unilume::benchmark::allocation::Operation::malloc_call, 17) && ok;
    }
    std::free(c_malloc);

    void *c_calloc = nullptr;
    {
        MeasurementScope measured;
        c_calloc = std::calloc(3, 7);
        unilume::benchmark::allocation::observeAllocation(c_calloc, 21);
        ok = expect("calloc fixture", measured.finish(),
                    unilume::benchmark::allocation::Operation::calloc_call, 21) && ok;
    }
    std::free(c_calloc);

    c_malloc = std::malloc(17);
    void *c_realloc = nullptr;
    {
        MeasurementScope measured;
        c_realloc = std::realloc(c_malloc, 41);
        unilume::benchmark::allocation::observeAllocation(c_realloc, 41);
        ok = expect("realloc fixture", measured.finish(),
                    unilume::benchmark::allocation::Operation::realloc_call, 41) && ok;
    }
    std::free(c_realloc);

    void *cpp_scalar = nullptr;
    {
        MeasurementScope measured;
        cpp_scalar = ::operator new(sizeof(int));
        unilume::benchmark::allocation::observeAllocation(cpp_scalar, sizeof(int));
        ok = expect("scalar new fixture", measured.finish(),
                    unilume::benchmark::allocation::Operation::scalar_new,
                    sizeof(int)) && ok;
    }
    ::operator delete(cpp_scalar);

    void *cpp_array = nullptr;
    {
        MeasurementScope measured;
        cpp_array = ::operator new[](13);
        unilume::benchmark::allocation::observeAllocation(cpp_array, 13);
        ok = expect("array new fixture", measured.finish(),
                    unilume::benchmark::allocation::Operation::array_new, 13) && ok;
    }
    ::operator delete[](cpp_array);

    void *outside = std::malloc(29);
    MeasurementScope empty;
    const Snapshot ignored = empty.finish();
    std::free(outside);
    if (ignored.allocations != 0 || ignored.allocated_bytes != 0) {
        std::cerr << "outside scope: recorded allocations unexpectedly\n";
        ok = false;
    }

    const double overhead =
        unilume::benchmark::allocation::measureEmptyScopeOverheadNanoseconds();
    if (overhead < 0.0) {
        std::cerr << "empty scope unexpectedly recorded an allocation\n";
        ok = false;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
