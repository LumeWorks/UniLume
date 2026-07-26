// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace unilume::fuzz {

inline constexpr std::size_t max_input_bytes = 64 * 1024;
inline constexpr std::size_t max_operations = 4096;

struct Outcome {
    bool valid{};
    std::string fingerprint;

    friend bool operator==(const Outcome &, const Outcome &) = default;
};

[[nodiscard]] Outcome runEngine(std::span<const std::uint8_t> data);
[[nodiscard]] Outcome runParsers(std::span<const std::uint8_t> data);
[[nodiscard]] Outcome runTransactions(std::span<const std::uint8_t> data);
[[nodiscard]] bool knownTransactionFaultsDetected();
void requireValid(const Outcome &outcome);

} // namespace unilume::fuzz
