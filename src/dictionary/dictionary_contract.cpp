// SPDX-License-Identifier: GPL-2.0-or-later

#include "dictionary_contract.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace unilume::dictionary {
namespace {

constexpr std::string_view header = "unilume_dictionary_version=1\n";

bool precomposedWordScalar(char32_t scalar)
{
    if (scalar <= 0x7f) {
        return std::isalnum(static_cast<unsigned char>(scalar)) != 0;
    }
    if ((scalar >= 0x00c0 && scalar <= 0x00d6) ||
        (scalar >= 0x00d8 && scalar <= 0x00f6) ||
        (scalar >= 0x00f8 && scalar <= 0x00ff) ||
        (scalar >= 0x1ea0 && scalar <= 0x1ef9)) {
        return true;
    }
    switch (scalar) {
    case 0x0102:
    case 0x0103:
    case 0x0110:
    case 0x0111:
    case 0x0128:
    case 0x0129:
    case 0x0168:
    case 0x0169:
    case 0x01a0:
    case 0x01a1:
    case 0x01af:
    case 0x01b0:
        return true;
    default:
        return false;
    }
}

bool validPrecomposedWord(std::string_view text)
{
    for (std::size_t offset = 0; offset < text.size();) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        std::size_t width = 0;
        char32_t scalar = 0;
        if (lead <= 0x7f) {
            width = 1;
            scalar = lead;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            width = 2;
            scalar = lead & 0x1f;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            width = 3;
            scalar = lead & 0x0f;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            width = 4;
            scalar = lead & 0x07;
        } else {
            return false;
        }
        if (offset + width > text.size()) {
            return false;
        }
        for (std::size_t index = 1; index < width; ++index) {
            const auto continuation =
                static_cast<unsigned char>(text[offset + index]);
            if ((continuation & 0xc0) != 0x80) {
                return false;
            }
            scalar = (scalar << 6) | (continuation & 0x3f);
        }
        if ((width == 3 && scalar < 0x800) ||
            (width == 4 && scalar < 0x10000) || scalar > 0x10ffff ||
            (scalar >= 0xd800 && scalar <= 0xdfff) || scalar == 0) {
            return false;
        }
        if (!precomposedWordScalar(scalar)) {
            return false;
        }
        offset += width;
    }
    return true;
}

bool literalWord(std::string_view word)
{
    if (word.empty() || word.size() > max_word_bytes) {
        return false;
    }
    for (const unsigned char character : word) {
        if (character > 0x7f || std::isalnum(character) == 0) {
            return false;
        }
    }
    return true;
}

bool keepWord(std::string_view word)
{
    if (word.empty() || word.size() > max_word_bytes ||
        !validPrecomposedWord(word)) {
        return false;
    }
    return true;
}

std::string asciiFold(std::string_view word)
{
    std::string folded;
    folded.reserve(word.size());
    for (const unsigned char character : word) {
        folded += static_cast<char>(std::tolower(character));
    }
    return folded;
}

int foldedCompare(std::string_view left, std::string_view right)
{
    const std::size_t common = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto left_character = static_cast<unsigned char>(left[index]);
        const auto right_character = static_cast<unsigned char>(right[index]);
        const int folded_left = std::tolower(left_character);
        const int folded_right = std::tolower(right_character);
        if (folded_left != folded_right) {
            return folded_left < folded_right ? -1 : 1;
        }
    }
    if (left.size() == right.size()) {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

} // namespace

std::string validate(const Snapshot &snapshot)
{
    if (!snapshot.table) {
        return "dictionary table is missing";
    }
    const Table &table = *snapshot.table;
    if (table.keep_words.size() + table.restore_words.size() > max_entries) {
        return "too many dictionary entries";
    }
    if (!std::is_sorted(table.keep_words.begin(), table.keep_words.end()) ||
        std::adjacent_find(table.keep_words.begin(), table.keep_words.end()) !=
            table.keep_words.end()) {
        return "keep entries must be sorted and unique";
    }
    if (!std::is_sorted(table.restore_words.begin(),
                        table.restore_words.end()) ||
        std::adjacent_find(table.restore_words.begin(),
                           table.restore_words.end()) !=
            table.restore_words.end()) {
        return "restore entries must be sorted and unique";
    }
    for (const std::string &word : table.keep_words) {
        if (!keepWord(word)) {
            return "keep entry must be bounded precomposed UTF-8";
        }
    }
    for (const std::string &word : table.restore_words) {
        if (!literalWord(word) || asciiFold(word) != word) {
            return "restore entry must be folded ASCII letters or digits";
        }
    }
    return {};
}

Snapshot makeSnapshot(bool enabled, std::vector<std::string> keep_words,
                      std::vector<std::string> restore_words)
{
    auto table = std::make_shared<Table>();
    table->keep_words = std::move(keep_words);
    table->restore_words = std::move(restore_words);
    return {enabled, std::move(table)};
}

DecodeResult decode(std::string_view text)
{
    if (text.size() > max_serialized_bytes) {
        return {.field = "file", .error = "dictionary exceeds size limit"};
    }
    if (!text.starts_with(header)) {
        return {.line = 1,
                .field = "version",
                .error = "unsupported dictionary version"};
    }
    std::vector<std::string> keep_words;
    std::vector<std::string> restore_words;
    std::size_t offset = header.size();
    std::size_t line_number = 1;
    while (offset < text.size()) {
        ++line_number;
        const std::size_t end = text.find('\n', offset);
        std::string_view line = text.substr(
            offset, end == std::string_view::npos ? text.size() - offset
                                                  : end - offset);
        offset = end == std::string_view::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t separator = line.find('\t');
        if (separator == std::string_view::npos ||
            line.find('\t', separator + 1) != std::string_view::npos) {
            return {.line = line_number,
                    .field = "entry",
                    .error = "entry must contain one tab"};
        }
        const std::string_view behavior = line.substr(0, separator);
        const std::string_view word = line.substr(separator + 1);
        if (word.empty() || word.size() > max_word_bytes) {
            return {.line = line_number,
                    .field = "word",
                    .error = "word is empty or exceeds byte limit"};
        }
        if (behavior == "keep") {
            if (!keepWord(word)) {
                return {.line = line_number,
                        .field = "word",
                        .error = "keep word must be precomposed UTF-8"};
            }
            keep_words.emplace_back(word);
        } else if (behavior == "restore") {
            if (!literalWord(word)) {
                return {.line = line_number,
                        .field = "word",
                        .error =
                            "restore word must be ASCII letters or digits"};
            }
            restore_words.push_back(asciiFold(word));
        } else {
            return {.line = line_number,
                    .field = "behavior",
                    .error = "behavior must be keep or restore"};
        }
        if (keep_words.size() + restore_words.size() > max_entries) {
            return {.line = line_number,
                    .field = "file",
                    .error = "too many dictionary entries"};
        }
    }
    std::sort(keep_words.begin(), keep_words.end());
    std::sort(restore_words.begin(), restore_words.end());
    if (std::adjacent_find(keep_words.begin(), keep_words.end()) !=
            keep_words.end() ||
        std::adjacent_find(restore_words.begin(), restore_words.end()) !=
            restore_words.end()) {
        return {.line = line_number,
                .field = "word",
                .error = "duplicate dictionary entry"};
    }
    if (keep_words.empty() && restore_words.empty()) {
        return {.line = line_number,
                .field = "file",
                .error = "dictionary has no entries"};
    }
    return {.snapshot = makeSnapshot(true, std::move(keep_words),
                                     std::move(restore_words))};
}

std::string encode(const Snapshot &snapshot)
{
    if (!validate(snapshot).empty()) {
        return {};
    }
    const Table &table = *snapshot.table;
    std::string result(header);
    for (const std::string &word : table.keep_words) {
        result += "keep\t";
        result += word;
        result += '\n';
    }
    for (const std::string &word : table.restore_words) {
        result += "restore\t";
        result += word;
        result += '\n';
    }
    return result;
}

bool keeps(const Snapshot &snapshot, std::string_view composed_word)
{
    return snapshot.enabled && snapshot.table &&
           std::binary_search(snapshot.table->keep_words.begin(),
                              snapshot.table->keep_words.end(), composed_word);
}

bool restores(const Snapshot &snapshot, std::string_view raw_word)
{
    if (!snapshot.enabled || !snapshot.table || !literalWord(raw_word)) {
        return false;
    }
    const auto found =
        std::lower_bound(snapshot.table->restore_words.begin(),
                         snapshot.table->restore_words.end(), raw_word,
                         [](const std::string &folded, std::string_view raw) {
                             return foldedCompare(folded, raw) < 0;
                         });
    return found != snapshot.table->restore_words.end() &&
           foldedCompare(raw_word, *found) == 0;
}

} // namespace unilume::dictionary
