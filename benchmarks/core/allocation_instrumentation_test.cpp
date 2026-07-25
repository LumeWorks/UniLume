// SPDX-License-Identifier: GPL-2.0-or-later

#include "allocation_instrumentation.h"

#include <cstdlib>
#include <iostream>
#include <new>

namespace {

bool expect(const char *name,
            const unilume::benchmark::allocation::Snapshot &actual,
            std::uint64_t allocations,
            std::uint64_t bytes)
{
    if (actual.allocations == allocations && actual.allocated_bytes == bytes) {
        return true;
    }
    std::cerr << name << ": expected allocations=" << allocations
              << " bytes=" << bytes << " but got allocations="
              << actual.allocations << " bytes=" << actual.allocated_bytes
              << '\n';
    return false;
}

} // namespace

int main()
{
    using unilume::benchmark::allocation::MeasurementScope;
    using unilume::benchmark::allocation::Snapshot;

    void *c_malloc = nullptr;
    void *c_calloc = nullptr;
    void *c_realloc = nullptr;
    void *cpp_scalar = nullptr;
    void *cpp_array = nullptr;

    MeasurementScope measured;
    c_malloc = std::malloc(17);
    c_calloc = std::calloc(3, 7);
    c_realloc = std::realloc(c_malloc, 41);
    c_malloc = nullptr;
    cpp_scalar = ::operator new(sizeof(int));
    cpp_array = ::operator new[](13);
    const Snapshot mixed = measured.finish();

    std::free(c_calloc);
    std::free(c_realloc);
    ::operator delete(cpp_scalar);
    ::operator delete[](cpp_array);

    bool ok = expect("mixed C/C++ fixture", mixed, 5, 17 + 21 + 41 + 4 + 13);

    void *outside = std::malloc(29);
    MeasurementScope empty;
    const Snapshot ignored = empty.finish();
    std::free(outside);
    ok = expect("outside scope", ignored, 0, 0) && ok;

    const double overhead =
        unilume::benchmark::allocation::measureEmptyScopeOverheadNanoseconds();
    if (overhead < 0.0) {
        std::cerr << "empty scope unexpectedly recorded an allocation\n";
        ok = false;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
