// SPDX-License-Identifier: GPL-2.0-or-later

#include "fuzz_harness.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <vector>

#ifndef UNILUME_FUZZ_CORPUS_DIR
#define UNILUME_FUZZ_CORPUS_DIR "fuzz/corpus"
#endif

namespace {

using Runner = unilume::fuzz::Outcome (*)(
    std::span<const std::uint8_t>);

bool check(std::string_view name,
           Runner runner,
           std::span<const std::uint8_t> input)
{
    const auto first = runner(input);
    const auto second = runner(input);
    if (!first.valid || first != second) {
        std::cerr << name << " property failed for";
        for (const std::uint8_t byte : input) {
            std::cerr << ' ' << std::hex << static_cast<unsigned>(byte);
        }
        std::cerr << std::dec << "\ntrace: " << first.fingerprint << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool valid = true;
    const std::array<std::pair<std::string_view, Runner>, 3> runners{{
        {"engine", unilume::fuzz::runEngine},
        {"parsers", unilume::fuzz::runParsers},
        {"transaction", unilume::fuzz::runTransactions},
    }};

    for (const auto &[name, runner] : runners) {
        const std::filesystem::path corpus =
            std::filesystem::path(UNILUME_FUZZ_CORPUS_DIR) / name;
        for (const auto &entry : std::filesystem::directory_iterator(corpus)) {
            std::ifstream file(entry.path(), std::ios::binary);
            const std::vector<std::uint8_t> input{
                std::istreambuf_iterator<char>(file), {}};
            valid = check(name, runner, input) && valid;
        }
    }

    std::mt19937_64 random(0x554e494c554d45ULL);
    for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
        std::vector<std::uint8_t> input(random() % 512);
        for (auto &byte : input) {
            byte = static_cast<std::uint8_t>(random());
        }
        for (const auto &[name, runner] : runners) {
            valid = check(name, runner, input) && valid;
        }
    }

    // Deliberately stale and duplicate callbacks must be observed and rejected
    // without modifying the deterministic application document.
    valid = unilume::fuzz::knownTransactionFaultsDetected() && valid;

    return valid ? 0 : 1;
}
