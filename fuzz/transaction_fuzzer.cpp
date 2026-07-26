// SPDX-License-Identifier: GPL-2.0-or-later

#include "fuzz_harness.h"

extern "C" int LLVMFuzzerTestOneInput(const unsigned char *data, std::size_t size)
{
    const auto input = std::span(data, size);
    const auto first = unilume::fuzz::runTransactions(input);
    const auto second = unilume::fuzz::runTransactions(input);
    unilume::fuzz::requireValid(
        {first.valid && first == second, first.fingerprint});
    return 0;
}
