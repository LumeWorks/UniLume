// SPDX-License-Identifier: GPL-2.0-or-later

#include "backup_codec.h"

#include "utf8_validation.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <set>
#include <vector>

namespace unilume::config_gui {
namespace {

constexpr std::string_view version_one_header =
    "unilume_backup_version=1\n";
constexpr std::string_view version_zero_header =
    "unilume_backup_version=0\n";
constexpr std::array<std::string_view, 4> resource_names{
    "macros", "dictionary", "keymap", "application-policy"};
constexpr std::array<std::string_view, 4> managed_path_fields{
    "MacroFile", "DictionaryFile", "KeymapFile",
    "ApplicationPolicyFile"};

bool isManagedPath(std::string_view key)
{
    return std::find(managed_path_fields.begin(),
                     managed_path_fields.end(),
                     key) != managed_path_fields.end();
}

void appendRecord(std::string &output,
                  std::string_view type,
                  std::string_view name,
                  std::string_view payload)
{
    output += type;
    output += ' ';
    output += std::to_string(name.size());
    output += ' ';
    output += std::to_string(payload.size());
    output += '\n';
    output += name;
    output += payload;
    output += '\n';
}

bool parseSize(std::string_view text, std::size_t &value)
{
    if (text.empty() || text.size() > 16) {
        return false;
    }
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

struct Record {
    std::string type;
    std::string name;
    std::string payload;
};

bool readRecord(std::string_view text,
                std::size_t &offset,
                Record &record,
                std::string &error)
{
    const std::size_t line_end = text.find('\n', offset);
    if (line_end == std::string_view::npos) {
        error = "backup record header is incomplete";
        return false;
    }
    const std::string_view header =
        text.substr(offset, line_end - offset);
    offset = line_end + 1;
    const std::size_t first = header.find(' ');
    const std::size_t second =
        first == std::string_view::npos
            ? std::string_view::npos
            : header.find(' ', first + 1);
    if (first == std::string_view::npos ||
        second == std::string_view::npos ||
        header.find(' ', second + 1) != std::string_view::npos) {
        error = "backup record header is malformed";
        return false;
    }
    std::size_t name_size = 0;
    std::size_t payload_size = 0;
    if (!parseSize(header.substr(first + 1, second - first - 1),
                   name_size) ||
        !parseSize(header.substr(second + 1), payload_size) ||
        name_size == 0 || name_size > 128 ||
        payload_size > max_backup_bytes ||
        name_size + payload_size > text.size() - offset) {
        error = "backup record size is invalid";
        return false;
    }
    record.type = std::string(header.substr(0, first));
    record.name = std::string(text.substr(offset, name_size));
    offset += name_size;
    record.payload = std::string(text.substr(offset, payload_size));
    offset += payload_size;
    if (offset >= text.size() || text[offset] != '\n') {
        error = "backup record payload is incomplete";
        return false;
    }
    ++offset;
    if (!core::isValidUtf8(record.name) ||
        !core::isValidUtf8(record.payload)) {
        error = "backup contains invalid UTF-8";
        return false;
    }
    return true;
}

void assignResource(ResourceDocuments &resources,
                    std::string_view name,
                    std::string payload)
{
    if (name == "macros") {
        resources.macros = std::move(payload);
    } else if (name == "dictionary") {
        resources.dictionary = std::move(payload);
    } else if (name == "keymap") {
        resources.keymap = std::move(payload);
    } else if (name == "application-policy") {
        resources.application_policy = std::move(payload);
    }
}

std::string_view resourceValue(const ResourceDocuments &resources,
                               std::string_view name)
{
    if (name == "macros") {
        return resources.macros;
    }
    if (name == "dictionary") {
        return resources.dictionary;
    }
    if (name == "keymap") {
        return resources.keymap;
    }
    return resources.application_policy;
}

} // namespace

std::string encodeBackup(const Settings &settings)
{
    if (!validate(settings).ok()) {
        return {};
    }
    std::string output(version_one_header);
    for (const std::string_view key : allConfigKeys()) {
        appendRecord(output, "field", key,
                     isManagedPath(key) ? std::string_view{}
                                        : value(settings, key));
    }
    for (const std::string_view name : resource_names) {
        appendRecord(output, "resource", name,
                     resourceValue(settings.resources, name));
    }
    output += "end\n";
    return output.size() <= max_backup_bytes ? output : std::string{};
}

BackupDecodeResult decodeBackup(std::string_view text)
{
    if (text.size() > max_backup_bytes) {
        return {.error = "backup exceeds size limit"};
    }
    bool migrated = false;
    std::size_t offset = 0;
    if (text.starts_with(version_one_header)) {
        offset = version_one_header.size();
    } else if (text.starts_with(version_zero_header)) {
        offset = version_zero_header.size();
        migrated = true;
    } else {
        return {.error = "unsupported backup version"};
    }

    std::map<std::string, std::string> fields;
    ResourceDocuments resources;
    std::set<std::string> seen_resources;
    while (offset < text.size() &&
           !text.substr(offset).starts_with("end\n")) {
        Record record;
        std::string error;
        if (!readRecord(text, offset, record, error)) {
            return {.error = std::move(error)};
        }
        if (record.type == "field") {
            if (!fields.emplace(std::move(record.name),
                                std::move(record.payload))
                     .second) {
                return {.error = "backup contains a duplicate field"};
            }
        } else if (record.type == "resource") {
            if (!seen_resources.emplace(record.name).second ||
                std::find(resource_names.begin(), resource_names.end(),
                          record.name) == resource_names.end()) {
                return {.error =
                            "backup contains an unknown or duplicate resource"};
            }
            assignResource(resources, record.name,
                           std::move(record.payload));
        } else {
            return {.error = "backup contains an unknown record type"};
        }
    }
    if (!text.substr(offset).starts_with("end\n") ||
        offset + 4 != text.size()) {
        return {.error = "backup terminator is missing or followed by data"};
    }

    std::set<std::string> known;
    for (const std::string_view key : allConfigKeys()) {
        known.emplace(key);
    }
    for (const auto &[key, unused] : fields) {
        (void)unused;
        if (!known.contains(key)) {
            return {.error = "backup contains an unknown field"};
        }
    }
    if (seen_resources.size() != resource_names.size()) {
        return {.error = "backup is missing a resource section"};
    }
    if (!migrated && fields.size() != allConfigKeys().size()) {
        return {.error = "backup is missing a configuration field"};
    }
    if (migrated) {
        fields.erase("EmojiEnabled");
        fields.erase("EmojiHotkey");
    }

    Settings settings = settingsFromValues(fields);
    for (const std::string_view key : managed_path_fields) {
        settings.values[std::string(key)].clear();
    }
    settings.resources = std::move(resources);
    const ValidationResult validation = validate(settings);
    if (!validation.ok()) {
        return {.error = validation.errors.front().field + ": " +
                         validation.errors.front().message};
    }
    return {std::move(settings), migrated, {}};
}

} // namespace unilume::config_gui
