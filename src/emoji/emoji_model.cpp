// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_model.h"

#include "utf8_validation.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <set>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace unilume::emoji {
namespace {

constexpr std::string_view history_header =
    "unilume_emoji_history_version=1\n";

std::string normalize(std::string_view text)
{
    std::string result(text);
    for (char &character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

bool validField(std::string_view value, std::size_t limit)
{
    return !value.empty() && value.size() <= limit &&
           value.find_first_of("\r\n\0") == std::string_view::npos &&
           core::isValidUtf8(value);
}

std::vector<std::uint32_t> codepoints(std::string_view text)
{
    std::vector<std::uint32_t> result;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        std::size_t length = 1;
        std::uint32_t value = lead;
        if (lead >= 0xc2 && lead <= 0xdf) {
            length = 2;
            value = lead & 0x1f;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            length = 3;
            value = lead & 0x0f;
        } else if (lead >= 0xf0) {
            length = 4;
            value = lead & 0x07;
        }
        for (std::size_t index = 1; index < length; ++index) {
            value = (value << 6) |
                    (static_cast<unsigned char>(text[offset + index]) & 0x3f);
        }
        result.push_back(value);
        offset += length;
    }
    return result;
}

std::optional<std::pair<unsigned, std::size_t>>
matchScore(std::string_view keyword, std::string_view query)
{
    if (keyword == query) {
        return std::pair{0U, std::size_t{0}};
    }
    if (keyword.starts_with(query)) {
        return std::pair{1U, keyword.size() - query.size()};
    }
    for (std::size_t offset = 1; offset < keyword.size(); ++offset) {
        if ((keyword[offset - 1] == ' ' || keyword[offset - 1] == '-' ||
             keyword[offset - 1] == '_') &&
            keyword.substr(offset).starts_with(query)) {
            return std::pair{2U, offset};
        }
    }
    if (const std::size_t offset = keyword.find(query);
        offset != std::string_view::npos) {
        return std::pair{3U, offset};
    }
    const std::vector<std::uint32_t> keyword_codepoints =
        codepoints(keyword);
    const std::vector<std::uint32_t> query_codepoints =
        codepoints(query);
    std::size_t query_offset = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    for (std::size_t offset = 0;
         offset < keyword_codepoints.size() &&
         query_offset < query_codepoints.size(); ++offset) {
        if (keyword_codepoints[offset] == query_codepoints[query_offset]) {
            if (query_offset == 0) {
                first = offset;
            }
            last = offset;
            ++query_offset;
        }
    }
    if (query_offset == query_codepoints.size()) {
        return std::pair{
            4U, last - first + 1 - query_codepoints.size()};
    }
    return std::nullopt;
}

std::string errorText(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

bool writeAll(int descriptor, std::string_view text, std::string &error)
{
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t result =
            write(descriptor, text.data() + written, text.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errorText("write emoji history");
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

bool SearchIndex::add(std::string_view keyword,
                      const std::vector<std::string> &glyphs)
{
    if (!validField(keyword, max_keyword_bytes)) {
        return false;
    }
    bool complete = true;
    const std::string normalized_keyword = normalize(keyword);
    for (const std::string &glyph : glyphs) {
        if (!validField(glyph, max_glyph_bytes)) {
            complete = false;
            continue;
        }
        if (entries_.size() >= max_index_entries) {
            return false;
        }
        entries_.push_back(
            {std::string(keyword), normalized_keyword, glyph});
    }
    return complete;
}

std::vector<SearchResult>
SearchIndex::search(std::string_view query, std::size_t limit) const
{
    if (!validField(query, max_query_bytes) || limit == 0) {
        return {};
    }
    limit = std::min(limit, max_search_results);
    const std::string normalized_query = normalize(query);
    struct Ranked {
        unsigned rank{};
        std::size_t distance{};
        const Entry *entry{};
    };
    std::vector<Ranked> ranked;
    ranked.reserve(std::min(entries_.size(), max_search_results * 4));
    for (const Entry &entry : entries_) {
        const auto score =
            matchScore(entry.normalized_keyword, normalized_query);
        if (score) {
            ranked.push_back({score->first, score->second, &entry});
        }
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const Ranked &left, const Ranked &right) {
                  return std::tie(left.rank, left.distance,
                                  left.entry->normalized_keyword,
                                  left.entry->glyph) <
                         std::tie(right.rank, right.distance,
                                  right.entry->normalized_keyword,
                                  right.entry->glyph);
              });
    std::set<std::string> seen;
    std::vector<SearchResult> result;
    result.reserve(std::min(limit, ranked.size()));
    for (const Ranked &candidate : ranked) {
        if (!seen.emplace(candidate.entry->glyph).second) {
            continue;
        }
        result.push_back(
            {candidate.entry->glyph, candidate.entry->keyword});
        if (result.size() == limit) {
            break;
        }
    }
    return result;
}

std::string validateHistory(const HistorySnapshot &snapshot)
{
    if (snapshot.recent.size() > max_history_entries) {
        return "emoji history exceeds entry limit";
    }
    std::set<std::string> seen;
    for (const std::string &glyph : snapshot.recent) {
        if (!validField(glyph, max_glyph_bytes)) {
            return "emoji history contains an invalid UTF-8 entry";
        }
        if (!seen.emplace(glyph).second) {
            return "emoji history contains a duplicate entry";
        }
    }
    return {};
}

std::string encodeHistory(const HistorySnapshot &snapshot)
{
    if (!validateHistory(snapshot).empty()) {
        return {};
    }
    std::string result(history_header);
    for (const std::string &glyph : snapshot.recent) {
        result += glyph;
        result += '\n';
    }
    return result;
}

HistoryLoadResult decodeHistory(std::string_view text)
{
    if (text.size() > max_history_bytes) {
        return {HistoryLoadDisposition::rejected, {},
                "emoji history exceeds size limit"};
    }
    if (!text.starts_with(history_header)) {
        return {HistoryLoadDisposition::rejected, {},
                "unsupported emoji history version"};
    }
    HistorySnapshot snapshot;
    std::size_t offset = history_header.size();
    while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        if (end == std::string_view::npos) {
            return {HistoryLoadDisposition::rejected, {},
                    "emoji history has an incomplete final entry"};
        }
        const std::string_view glyph = text.substr(offset, end - offset);
        if (glyph.empty()) {
            return {HistoryLoadDisposition::rejected, {},
                    "emoji history contains an empty entry"};
        }
        snapshot.recent.emplace_back(glyph);
        offset = end + 1;
    }
    if (const std::string error = validateHistory(snapshot); !error.empty()) {
        return {HistoryLoadDisposition::rejected, {}, error};
    }
    return {HistoryLoadDisposition::loaded, std::move(snapshot), {}};
}

HistoryStore::HistoryStore(std::filesystem::path path)
    : path_(std::move(path))
{
}

HistoryLoadResult HistoryStore::load()
{
    std::error_code filesystem_error;
    const bool exists = std::filesystem::exists(path_, filesystem_error);
    if (filesystem_error) {
        return {HistoryLoadDisposition::rejected, active_,
                "inspect emoji history: " + filesystem_error.message()};
    }
    if (!exists) {
        active_ = {};
        return {HistoryLoadDisposition::missing, active_, {}};
    }
    const std::uintmax_t size =
        std::filesystem::file_size(path_, filesystem_error);
    if (filesystem_error || size > max_history_bytes) {
        return {HistoryLoadDisposition::rejected, active_,
                filesystem_error
                    ? "inspect emoji history size: " +
                          filesystem_error.message()
                    : "emoji history exceeds size limit"};
    }
    errno = 0;
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        return {HistoryLoadDisposition::rejected, active_,
                errorText("read emoji history")};
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
        return {HistoryLoadDisposition::rejected, active_,
                "emoji history changed while reading"};
    }
    const HistoryLoadResult decoded = decodeHistory(text);
    if (!decoded.ok()) {
        return {HistoryLoadDisposition::rejected, active_, decoded.error};
    }
    active_ = decoded.snapshot;
    return {HistoryLoadDisposition::loaded, active_, {}};
}

bool HistoryStore::record(std::string_view glyph, std::string *error)
{
    if (!validField(glyph, max_glyph_bytes)) {
        if (error) {
            *error = "emoji history entry is invalid";
        }
        return false;
    }
    HistorySnapshot next = active_;
    std::erase(next.recent, glyph);
    next.recent.insert(next.recent.begin(), std::string(glyph));
    if (next.recent.size() > max_history_entries) {
        next.recent.resize(max_history_entries);
    }
    if (!save(next, error)) {
        return false;
    }
    active_ = std::move(next);
    return true;
}

bool HistoryStore::clear(std::string *error)
{
    HistorySnapshot empty;
    if (!save(empty, error)) {
        return false;
    }
    active_ = {};
    return true;
}

bool HistoryStore::save(const HistorySnapshot &snapshot,
                        std::string *error) const
{
    if (const std::string validation = validateHistory(snapshot);
        !validation.empty()) {
        if (error) {
            *error = validation;
        }
        return false;
    }
    const std::filesystem::path parent = path_.has_parent_path()
                                             ? path_.parent_path()
                                             : std::filesystem::path{"."};
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        if (error) {
            *error = "create emoji history directory: " +
                     filesystem_error.message();
        }
        return false;
    }
    std::string template_path = path_.string() + ".tmp.XXXXXX";
    std::vector<char> temporary(template_path.begin(), template_path.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) {
        if (error) {
            *error = errorText("create temporary emoji history");
        }
        return false;
    }
    const std::filesystem::path temporary_path(temporary.data());
    std::string write_error;
    const std::string text = encodeHistory(snapshot);
    const bool written = fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
                         writeAll(descriptor, text, write_error) &&
                         fsync(descriptor) == 0;
    if (!written && write_error.empty()) {
        write_error = errorText("sync emoji history");
    }
    const int close_result = close(descriptor);
    if (!written || close_result != 0) {
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error) {
            *error = write_error.empty()
                         ? errorText("close emoji history")
                         : write_error;
        }
        return false;
    }
    if (rename(temporary_path.c_str(), path_.c_str()) != 0) {
        const std::string rename_error =
            errorText("replace emoji history");
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error) {
            *error = rename_error;
        }
        return false;
    }
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        // The rename above is the commit point. Directory fsync improves
        // crash durability where supported, but a late failure cannot roll
        // the already-visible replacement back without risking newer data.
        const int sync_result = fsync(directory);
        close(directory);
        (void)sync_result;
    }
    return true;
}

} // namespace unilume::emoji
