// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::dictionary {

inline constexpr std::size_t max_entries = 65536;
inline constexpr std::size_t max_word_bytes = 128;
inline constexpr std::size_t max_serialized_bytes = 8 * 1024 * 1024;

enum class Behavior
{
    keep_composed,
    restore_literal
};

struct Table
{
    std::vector<std::string> keep_words;
    std::vector<std::string> restore_words;

    friend bool operator==(const Table &, const Table &) = default;
};

struct Snapshot
{
    bool enabled{false};
    std::shared_ptr<const Table> table{std::make_shared<Table>()};

    friend bool operator==(const Snapshot &left, const Snapshot &right)
    {
        if (left.enabled != right.enabled ||
            static_cast<bool>(left.table) != static_cast<bool>(right.table)) {
            return false;
        }
        return !left.table || *left.table == *right.table;
    }
};

struct DecodeResult
{
    Snapshot snapshot;
    std::size_t line{};
    std::string field;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] std::string validate(const Snapshot &snapshot);
[[nodiscard]] Snapshot makeSnapshot(bool enabled,
                                    std::vector<std::string> keep_words,
                                    std::vector<std::string> restore_words);
[[nodiscard]] DecodeResult decode(std::string_view text);
[[nodiscard]] std::string encode(const Snapshot &snapshot);
[[nodiscard]] bool keeps(const Snapshot &snapshot,
                         std::string_view composed_word);
[[nodiscard]] bool restores(const Snapshot &snapshot,
                            std::string_view raw_word);

} // namespace unilume::dictionary
