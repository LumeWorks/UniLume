// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::keymap {

inline constexpr std::size_t max_entries = 64;
inline constexpr std::size_t max_serialized_bytes = 64 * 1024;

enum class Action : std::uint8_t {
    tone0, tone1, tone2, tone3, tone4, tone5,
    roof_all, roof_a, roof_e, roof_o,
    hook_all, hook_uo, hook_u, hook_o,
    bowl, d_mark, telex_w, escape,
    count,
};

struct Entry {
    char key{};
    Action action{};

    friend bool operator==(const Entry &, const Entry &) = default;
};

struct Snapshot {
    std::vector<Entry> entries;

    friend bool operator==(const Snapshot &, const Snapshot &) = default;
};

struct DecodeResult {
    Snapshot snapshot;
    std::size_t line{};
    std::string field;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] std::string validate(const Snapshot &snapshot);
[[nodiscard]] DecodeResult decode(std::string_view text);
[[nodiscard]] std::string encode(const Snapshot &snapshot);
[[nodiscard]] std::string_view actionName(Action action);

} // namespace unilume::keymap
