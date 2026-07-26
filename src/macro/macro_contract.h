// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::macro {

inline constexpr std::size_t max_entries = 1024;
inline constexpr std::size_t max_key_characters = 15;
inline constexpr std::size_t max_text_characters = 1023;
inline constexpr std::size_t max_serialized_bytes = 4 * 1024 * 1024;

enum class Trigger { word_boundary };
enum class Capitalization { exact };

struct Entry {
    std::string key;
    std::string text;

    friend bool operator==(const Entry &, const Entry &) = default;
};

struct Snapshot {
    bool enabled{false};
    Trigger trigger{Trigger::word_boundary};
    Capitalization capitalization{Capitalization::exact};
    std::vector<Entry> entries;

    friend bool operator==(const Snapshot &, const Snapshot &) = default;
};

struct DecodeResult {
    Snapshot snapshot;
    bool migrated{};
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] std::string validate(const Snapshot &snapshot);
[[nodiscard]] std::string encode(const Snapshot &snapshot);
[[nodiscard]] DecodeResult decode(std::string_view text);

} // namespace unilume::macro
