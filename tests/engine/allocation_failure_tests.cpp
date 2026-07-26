// SPDX-License-Identifier: GPL-2.0-or-later

#include "unilume_context.h"

#include <cstddef>
#include <iostream>
#include <new>

namespace {

bool fail_nothrow_allocation = false;

bool validContextCanBeCreated()
{
    UlEngineContext *context = nullptr;
    const UlStatus status =
        ul_engine_create(UL_INPUT_METHOD_TELEX, &context);
    const bool valid =
        status == UL_STATUS_OK && context != nullptr;
    ul_engine_destroy(context);
    return valid;
}

} // namespace

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    if (fail_nothrow_allocation) {
        return nullptr;
    }
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void *pointer, const std::nothrow_t &) noexcept
{
    ::operator delete(pointer);
}

int main()
{
    if (!validContextCanBeCreated()) {
        std::cerr << "control context creation failed\n";
        return 1;
    }

    fail_nothrow_allocation = true;
    UlEngineContext *context = reinterpret_cast<UlEngineContext *>(1);
    const UlStatus status =
        ul_engine_create(UL_INPUT_METHOD_TELEX, &context);
    fail_nothrow_allocation = false;
    if (status != UL_STATUS_OUT_OF_MEMORY || context != nullptr) {
        std::cerr << "controlled allocation failure was not contained\n";
        return 1;
    }

    if (!validContextCanBeCreated()) {
        std::cerr << "context creation did not recover after allocation failure\n";
        return 1;
    }
    return 0;
}
