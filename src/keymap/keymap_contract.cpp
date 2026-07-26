// SPDX-License-Identifier: GPL-2.0-or-later

#include "keymap_contract.h"

#include <array>
#include <cctype>
#include <set>

namespace unilume::keymap {
namespace {

constexpr std::array<std::string_view,
                     static_cast<std::size_t>(Action::count)> names{{
    "Tone0", "Tone1", "Tone2", "Tone3", "Tone4", "Tone5",
    "Roof-All", "Roof-A", "Roof-E", "Roof-O", "Hook-Bowl", "Hook-UO",
    "Hook-U", "Hook-O", "Bowl", "D-Mark", "Telex-W", "Escape",
}};

std::string_view trim(std::string_view value)
{
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    while (!value.empty() && value.back() == ' ') {
        value.remove_suffix(1);
    }
    return value;
}

bool parseAction(std::string_view name, Action &action)
{
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (name == names[index]) {
            action = static_cast<Action>(index);
            return true;
        }
    }
    return false;
}

} // namespace

std::string_view actionName(Action action)
{
    const auto index = static_cast<std::size_t>(action);
    return index < names.size() ? names[index] : std::string_view{};
}

std::string validate(const Snapshot &snapshot)
{
    if (snapshot.entries.empty()) {
        return "keymap must contain at least one entry";
    }
    if (snapshot.entries.size() > max_entries) {
        return "too many keymap entries";
    }
    std::set<unsigned char> occupied;
    for (const Entry &entry : snapshot.entries) {
        const auto key = static_cast<unsigned char>(entry.key);
        if (key < 0x21 || key > 0x7e || key == '=' || key == ';') {
            return "reserved or unreachable key";
        }
        if (actionName(entry.action).empty()) {
            return "unknown keymap action";
        }
        if (!occupied.insert(key).second) {
            return "duplicate key";
        }
        if (std::isalpha(key) != 0) {
            const auto folded =
                static_cast<unsigned char>(std::tolower(key));
            const auto counterpart =
                static_cast<unsigned char>(std::toupper(key));
            if ((folded != key && occupied.contains(folded)) ||
                (counterpart != key && occupied.contains(counterpart))) {
                return "case-insensitive key conflict";
            }
        }
    }
    return {};
}

DecodeResult decode(std::string_view text)
{
    if (text.size() > max_serialized_bytes) {
        return {.field = "file", .error = "keymap exceeds size limit"};
    }
    Snapshot snapshot;
    std::size_t offset = 0;
    std::size_t line_number = 0;
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
        if (const std::size_t comment = line.find(';');
            comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos ||
            line.find('=', separator + 1) != std::string_view::npos) {
            return {.line = line_number, .field = "entry",
                    .error = "entry must contain one '=' separator"};
        }
        const std::string_view key = trim(line.substr(0, separator));
        const std::string_view action_text =
            trim(line.substr(separator + 1));
        if (key.size() != 1) {
            return {.line = line_number, .field = "key",
                    .error = "key must be one unmodified ASCII character"};
        }
        Action action{};
        if (!parseAction(action_text, action)) {
            return {.line = line_number, .field = "action",
                    .error = "unknown keymap action"};
        }
        snapshot.entries.push_back({key.front(), action});
        if (const std::string error = validate(snapshot); !error.empty()) {
            return {.line = line_number, .field = "key", .error = error};
        }
    }
    if (const std::string error = validate(snapshot); !error.empty()) {
        return {.line = line_number, .field = "file", .error = error};
    }
    return {.snapshot = std::move(snapshot)};
}

std::string encode(const Snapshot &snapshot)
{
    if (!validate(snapshot).empty()) {
        return {};
    }
    std::string result =
        "; UniLume validated custom keymap (UniKey legacy-compatible)\n";
    for (const Entry &entry : snapshot.entries) {
        result += entry.key;
        result += " = ";
        result += actionName(entry.action);
        result += '\n';
    }
    return result;
}

} // namespace unilume::keymap
