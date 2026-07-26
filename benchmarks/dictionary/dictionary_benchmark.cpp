// SPDX-License-Identifier: GPL-2.0-or-later

#include "allocation_instrumentation.h"
#include "dictionary_contract.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using Clock = std::chrono::steady_clock;
    using namespace unilume;
    std::vector<std::string> restore_words;
    restore_words.reserve(dictionary::max_entries);
    for (std::size_t index = 0; index < dictionary::max_entries; ++index) {
        std::string word = "word" + std::to_string(index + 100000);
        restore_words.push_back(std::move(word));
    }
    std::sort(restore_words.begin(), restore_words.end());
    dictionary::Snapshot snapshot =
        dictionary::makeSnapshot(true, {}, std::move(restore_words));
    if (!dictionary::validate(snapshot).empty()) {
        std::cerr << "large dictionary fixture is invalid\n";
        return 1;
    }

    constexpr std::size_t samples = 100000;
    std::vector<std::uint64_t> latency;
    latency.reserve(samples);
    std::uint64_t checksum = 0;
    benchmark::allocation::MeasurementScope allocation_scope;
    for (std::size_t index = 0; index < samples; ++index) {
        const std::string &query =
            snapshot.table
                ->restore_words[index % snapshot.table->restore_words.size()];
        const auto start = Clock::now();
        const bool found = dictionary::restores(snapshot, query);
        const auto end = Clock::now();
        checksum += found;
        latency.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count()));
    }
    const benchmark::allocation::Snapshot allocations =
        allocation_scope.finish();
    std::sort(latency.begin(), latency.end());
    const std::uint64_t p99 = latency[(latency.size() * 99) / 100];
    std::size_t retained_bytes =
        snapshot.table->restore_words.capacity() * sizeof(std::string);
    for (const std::string &word : snapshot.table->restore_words) {
        retained_bytes += word.capacity() + 1;
    }
    std::cout << "entries=" << snapshot.table->restore_words.size()
              << " samples=" << samples << " p99_ns=" << p99
              << " allocations=" << allocations.allocations
              << " retained_bytes_upper_bound=" << retained_bytes << '\n';
    if (checksum != samples || allocations.allocations != 0 ||
        retained_bytes > 16 * 1024 * 1024) {
        return 1;
    }
#ifdef NDEBUG
    if (p99 > 10000) {
        std::cerr << "dictionary lookup exceeds 10 us Release p99 SLO\n";
        return 1;
    }
#endif
    return 0;
}
