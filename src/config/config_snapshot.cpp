// SPDX-License-Identifier: GPL-2.0-or-later

#include "config_snapshot.h"

#include <array>
#include <charconv>
#include <set>
#include <sstream>

namespace unilume::config {
namespace {

constexpr std::array<std::string_view, 8> v1_keys{
    "schema_version", "input_method", "output_charset", "spell_check",
    "free_marking", "modern_tone", "auto_restore", "macro_enabled"};
constexpr std::array<std::string_view, 9> v2_keys{
    "schema_version", "input_method", "output_charset", "spell_check",
    "free_marking", "modern_tone", "auto_restore", "macro_enabled",
    "macro_file"};

bool parseBool(std::string_view value, bool &out)
{
    if (value == "true") {
        out = true;
        return true;
    }
    if (value == "false") {
        out = false;
        return true;
    }
    return false;
}

bool isValidUtf8(std::string_view text)
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
            (width == 4 && scalar < 0x10000) ||
            scalar > 0x10ffff ||
            (scalar >= 0xd800 && scalar <= 0xdfff) ||
            scalar == 0) {
            return false;
        }
        offset += width;
    }
    return true;
}

bool parseInputMethod(std::string_view value, InputMethod &out)
{
    if (value == "telex") {
        out = InputMethod::telex;
    } else if (value == "vni") {
        out = InputMethod::vni;
    } else if (value == "viqr") {
        out = InputMethod::viqr;
    } else {
        return false;
    }
    return true;
}

bool parseCharset(std::string_view value, OutputCharset &out)
{
    if (value != "utf8") {
        return false;
    }
    out = OutputCharset::utf8;
    return true;
}

std::string inputMethodName(InputMethod method)
{
    switch (method) {
    case InputMethod::telex:
        return "telex";
    case InputMethod::vni:
        return "vni";
    case InputMethod::viqr:
        return "viqr";
    }
    return "";
}

std::string charsetName(OutputCharset charset)
{
    return charset == OutputCharset::utf8 ? "utf8" : "";
}

bool isKnownKey(std::string_view key)
{
    for (const auto known : v2_keys) {
        if (key == known) {
            return true;
        }
    }
    return false;
}

} // namespace

Snapshot defaults()
{
    return {};
}

std::string validate(const Snapshot &snapshot)
{
    if (snapshot.version != schema_version) {
        return "unsupported schema version";
    }
    if (inputMethodName(snapshot.input_method).empty()) {
        return "unsupported input_method";
    }
    if (charsetName(snapshot.output_charset).empty()) {
        return "unsupported output_charset";
    }
    if (snapshot.macro_file.find('\r') != std::string::npos ||
        snapshot.macro_file.find('\n') != std::string::npos ||
        snapshot.macro_file.find('\0') != std::string::npos ||
        !isValidUtf8(snapshot.macro_file)) {
        return "invalid macro_file";
    }
    return {};
}

DecodeResult decode(std::string_view text)
{
    Snapshot snapshot = defaults();
    std::set<std::string> seen;
    bool explicit_version = false;
    std::size_t offset = 0;

    while (offset < text.size()) {
        const std::size_t line_end = text.find('\n', offset);
        std::string_view line = text.substr(
            offset, line_end == std::string_view::npos ? text.size() - offset
                                                        : line_end - offset);
        offset = line_end == std::string_view::npos ? text.size() : line_end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0 ||
            (separator + 1 == line.size() &&
             line.substr(0, separator) != "macro_file")) {
            return {.error = "invalid configuration line"};
        }
        const std::string_view key = line.substr(0, separator);
        const std::string_view value = line.substr(separator + 1);
        if (!isKnownKey(key)) {
            return {.error = "unknown configuration key: " + std::string(key)};
        }
        if (!seen.insert(std::string(key)).second) {
            return {.error = "duplicate configuration key: " + std::string(key)};
        }
        if (key == "schema_version") {
            std::uint32_t version{};
            const auto [cursor, error] = std::from_chars(
                value.data(), value.data() + value.size(), version);
            if (error != std::errc{} || cursor != value.data() + value.size()) {
                return {.error = "invalid schema_version"};
            }
            snapshot.version = version;
            explicit_version = true;
        } else if (key == "input_method" && !parseInputMethod(value, snapshot.input_method)) {
            return {.error = "invalid input_method"};
        } else if (key == "output_charset" && !parseCharset(value, snapshot.output_charset)) {
            return {.error = "invalid output_charset"};
        } else if (key == "spell_check" && !parseBool(value, snapshot.spell_check)) {
            return {.error = "invalid spell_check"};
        } else if (key == "free_marking" && !parseBool(value, snapshot.free_marking)) {
            return {.error = "invalid free_marking"};
        } else if (key == "modern_tone" && !parseBool(value, snapshot.modern_tone)) {
            return {.error = "invalid modern_tone"};
        } else if (key == "auto_restore" && !parseBool(value, snapshot.auto_restore)) {
            return {.error = "invalid auto_restore"};
        } else if (key == "macro_enabled" && !parseBool(value, snapshot.macro_enabled)) {
            return {.error = "invalid macro_enabled"};
        } else if (key == "macro_file") {
            snapshot.macro_file = value;
        }
    }

    if (seen.empty()) {
        return {.error = "empty configuration"};
    }
    const bool legacy = !explicit_version || snapshot.version == 1;
    if (legacy && seen.contains("macro_file")) {
        return {.error = "unknown configuration key for legacy schema: macro_file"};
    }
    const auto requireKey = [&](std::string_view required) -> std::string {
        if (required == "schema_version" && !explicit_version) {
            return {};
        }
        if (!seen.contains(std::string(required))) {
            return "missing configuration key: " + std::string(required);
        }
        return {};
    };
    if (legacy) {
        for (const auto required : v1_keys) {
            if (const std::string error = requireKey(required); !error.empty()) {
                return {.error = error};
            }
        }
    } else if (snapshot.version == schema_version) {
        for (const auto required : v2_keys) {
            if (const std::string error = requireKey(required); !error.empty()) {
                return {.error = error};
            }
        }
    } else {
        return {.error = "unsupported schema version"};
    }
    if (legacy) {
        snapshot.version = schema_version;
        const std::string validation = validate(snapshot);
        return validation.empty() ? DecodeResult{snapshot, true, {}}
                                  : DecodeResult{{}, false, validation};
    }
    const std::string validation = validate(snapshot);
    return validation.empty() ? DecodeResult{snapshot, false, {}}
                              : DecodeResult{{}, false, validation};
}

std::string encode(const Snapshot &snapshot)
{
    if (const std::string error = validate(snapshot); !error.empty()) {
        return {};
    }
    std::ostringstream result;
    result << "schema_version=" << schema_version << '\n'
           << "input_method=" << inputMethodName(snapshot.input_method) << '\n'
           << "output_charset=" << charsetName(snapshot.output_charset) << '\n'
           << "spell_check=" << (snapshot.spell_check ? "true" : "false") << '\n'
           << "free_marking=" << (snapshot.free_marking ? "true" : "false") << '\n'
           << "modern_tone=" << (snapshot.modern_tone ? "true" : "false") << '\n'
           << "auto_restore=" << (snapshot.auto_restore ? "true" : "false") << '\n'
           << "macro_enabled=" << (snapshot.macro_enabled ? "true" : "false") << '\n'
           << "macro_file=" << snapshot.macro_file << '\n';
    return result.str();
}

} // namespace unilume::config
