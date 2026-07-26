// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::emoji {

inline constexpr std::size_t max_index_entries = 16384;
inline constexpr std::size_t max_glyph_bytes = 64;
inline constexpr std::size_t max_keyword_bytes = 128;
inline constexpr std::size_t max_query_bytes = 64;
inline constexpr std::size_t max_search_results = 128;
inline constexpr std::size_t max_history_entries = 64;
inline constexpr std::size_t max_history_bytes = 64 * 1024;

struct SearchResult {
    std::string glyph;
    std::string keyword;

    friend bool operator==(const SearchResult &, const SearchResult &) =
        default;
};

class SearchIndex final {
public:
    [[nodiscard]] bool add(std::string_view keyword,
                           const std::vector<std::string> &glyphs);
    [[nodiscard]] std::vector<SearchResult>
    search(std::string_view query,
           std::size_t limit = max_search_results) const;
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string keyword;
        std::string normalized_keyword;
        std::string glyph;
    };

    std::vector<Entry> entries_;
};

struct HistorySnapshot {
    std::vector<std::string> recent;

    friend bool operator==(const HistorySnapshot &,
                           const HistorySnapshot &) = default;
};

enum class HistoryLoadDisposition {
    loaded,
    missing,
    rejected,
};

struct HistoryLoadResult {
    HistoryLoadDisposition disposition{HistoryLoadDisposition::missing};
    HistorySnapshot snapshot;
    std::string error;

    [[nodiscard]] bool ok() const
    {
        return disposition != HistoryLoadDisposition::rejected;
    }
};

[[nodiscard]] std::string validateHistory(const HistorySnapshot &snapshot);
[[nodiscard]] std::string encodeHistory(const HistorySnapshot &snapshot);
[[nodiscard]] HistoryLoadResult decodeHistory(std::string_view text);

class HistoryStore final {
public:
    explicit HistoryStore(std::filesystem::path path);

    [[nodiscard]] HistoryLoadResult load();
    [[nodiscard]] bool record(std::string_view glyph,
                              std::string *error = nullptr);
    [[nodiscard]] bool clear(std::string *error = nullptr);
    [[nodiscard]] const HistorySnapshot &active() const { return active_; }

private:
    [[nodiscard]] bool save(const HistorySnapshot &snapshot,
                            std::string *error) const;

    std::filesystem::path path_;
    HistorySnapshot active_;
};

} // namespace unilume::emoji
