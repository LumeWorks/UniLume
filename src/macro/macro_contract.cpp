// SPDX-License-Identifier: GPL-2.0-or-later

#include "macro_contract.h"

#include "vnconv.h"

#include <array>
#include <cstdint>
#include <set>

namespace unilume::macro {
namespace {

constexpr std::string_view canonical_header = "unilume_macro_version=1\n";
constexpr std::size_t legacy_storage_bytes = 128 * 1024;

bool utf8Characters(std::string_view text, std::size_t &characters)
{
    characters = 0;
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
            (width == 4 && scalar < 0x10000) ||
            scalar > 0x10ffff ||
            (scalar >= 0xd800 && scalar <= 0xdfff) ||
            scalar == 0) {
            return false;
        }
        ++characters;
        offset += width;
    }
    return true;
}

std::string escape(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

bool unescape(std::string_view value, std::string &result)
{
    result.clear();
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\') {
            result += value[index];
            continue;
        }
        if (++index == value.size()) {
            return false;
        }
        switch (value[index]) {
        case '\\':
            result += '\\';
            break;
        case 't':
            result += '\t';
            break;
        case 'n':
            result += '\n';
            break;
        case 'r':
            result += '\r';
            break;
        default:
            return false;
        }
    }
    return true;
}

bool convertViqr(std::string_view input, std::string &output)
{
    if (input.size() > max_text_characters * 4) {
        return false;
    }
    std::array<unsigned char, max_text_characters * 4 + 4> converted{};
    int input_size = static_cast<int>(input.size());
    int output_size = static_cast<int>(converted.size());
    const int status = VnConvert(
        CONV_CHARSET_VIQR,
        CONV_CHARSET_UNIUTF8,
        reinterpret_cast<unsigned char *>(const_cast<char *>(input.data())),
        converted.data(),
        &input_size,
        &output_size);
    if (status != 0 || output_size < 0) {
        return false;
    }
    output.assign(
        reinterpret_cast<const char *>(converted.data()),
        static_cast<std::size_t>(output_size));
    return true;
}

DecodeResult decodeCanonical(std::string_view text)
{
    Snapshot snapshot;
    snapshot.enabled = true;
    std::size_t offset = canonical_header.size();
    while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        std::string_view line = text.substr(
            offset, end == std::string_view::npos ? text.size() - offset
                                                  : end - offset);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            return {.error = "empty macro entry"};
        }
        const std::size_t separator = line.find('\t');
        if (separator == std::string_view::npos ||
            line.find('\t', separator + 1) != std::string_view::npos) {
            return {.error = "macro entry must contain one tab separator"};
        }
        Entry entry;
        if (!unescape(line.substr(0, separator), entry.key) ||
            !unescape(line.substr(separator + 1), entry.text)) {
            return {.error = "invalid macro escape"};
        }
        snapshot.entries.push_back(std::move(entry));
        offset = end == std::string_view::npos ? text.size() : end + 1;
    }
    const std::string error = validate(snapshot);
    if (!error.empty()) {
        return {.error = error};
    }
    return {.snapshot = std::move(snapshot)};
}

DecodeResult decodeLegacy(std::string_view text)
{
    Snapshot snapshot;
    snapshot.enabled = true;
    bool utf8 = false;
    std::size_t offset = 0;
    const std::size_t first_end = text.find('\n');
    std::string_view first = text.substr(
        0, first_end == std::string_view::npos ? text.size() : first_end);
    if (first.starts_with("\xef\xbb\xbf")) {
        first.remove_prefix(3);
    }
    if (first.find("***") != std::string_view::npos &&
        first.find("version=1") != std::string_view::npos) {
        utf8 = true;
        offset = first_end == std::string_view::npos ? text.size() : first_end + 1;
    }
    while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        std::string_view line = text.substr(
            offset, end == std::string_view::npos ? text.size() - offset
                                                  : end - offset);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            return {.error = "empty legacy macro entry"};
        }
        const std::size_t separator = line.find(':');
        if (separator == std::string_view::npos) {
            return {.error = "legacy macro entry has no separator"};
        }
        Entry entry;
        if (utf8) {
            entry.key.assign(line.substr(0, separator));
            entry.text.assign(line.substr(separator + 1));
        } else if (!convertViqr(line.substr(0, separator), entry.key) ||
                   !convertViqr(line.substr(separator + 1), entry.text)) {
            return {.error = "cannot convert legacy VIQR macro entry"};
        }
        snapshot.entries.push_back(std::move(entry));
        offset = end == std::string_view::npos ? text.size() : end + 1;
    }
    const std::string error = validate(snapshot);
    if (!error.empty()) {
        return {.error = error};
    }
    return {.snapshot = std::move(snapshot), .migrated = true};
}

} // namespace

std::string validate(const Snapshot &snapshot)
{
    if (snapshot.trigger != Trigger::word_boundary ||
        snapshot.capitalization != Capitalization::exact) {
        return "unsupported macro behavior";
    }
    if (snapshot.entries.size() > max_entries) {
        return "too many macro entries";
    }
    std::set<std::string> keys;
    std::size_t storage_bytes = 0;
    for (const Entry &entry : snapshot.entries) {
        std::size_t key_characters = 0;
        std::size_t text_characters = 0;
        if (entry.key.empty() || entry.text.empty()) {
            return "macro key and text must not be empty";
        }
        if (!utf8Characters(entry.key, key_characters) ||
            !utf8Characters(entry.text, text_characters)) {
            return "macro key or text is not valid UTF-8";
        }
        if (key_characters > max_key_characters) {
            return "macro key exceeds character limit";
        }
        if (text_characters > max_text_characters) {
            return "macro text exceeds character limit";
        }
        storage_bytes +=
            (key_characters + text_characters + 2) * sizeof(std::uint32_t);
        if (storage_bytes > legacy_storage_bytes) {
            return "macro table exceeds engine storage limit";
        }
        if (!keys.insert(entry.key).second) {
            return "duplicate macro key";
        }
    }
    return {};
}

std::string encode(const Snapshot &snapshot)
{
    if (!validate(snapshot).empty()) {
        return {};
    }
    std::string result(canonical_header);
    for (const Entry &entry : snapshot.entries) {
        result += escape(entry.key);
        result += '\t';
        result += escape(entry.text);
        result += '\n';
    }
    return result;
}

DecodeResult decode(std::string_view text)
{
    if (text.size() > max_serialized_bytes) {
        return {.error = "macro table exceeds serialized size limit"};
    }
    if (text.starts_with(canonical_header)) {
        return decodeCanonical(text);
    }
    return decodeLegacy(text);
}

} // namespace unilume::macro
