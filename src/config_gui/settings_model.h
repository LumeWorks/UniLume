// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::config_gui {

enum class Category {
    general,
    typing,
    applications,
    macros,
    dictionary,
    keymap,
    shortcuts,
    appearance,
    backup,
};

enum class FieldKind {
    boolean,
    choice,
    hotkey,
    managed_path,
};

struct FieldDescriptor {
    std::string_view key;
    std::string_view label;
    Category category{};
    FieldKind kind{};
    std::vector<std::string_view> choices;
};

struct ResourceDocuments {
    std::string macros;
    std::string dictionary;
    std::string keymap;
    std::string application_policy;

    friend bool operator==(const ResourceDocuments &,
                           const ResourceDocuments &) = default;
};

struct Settings {
    std::map<std::string, std::string> values;
    ResourceDocuments resources;

    friend bool operator==(const Settings &, const Settings &) = default;
};

struct ValidationError {
    std::string field;
    std::string message;

    friend bool operator==(const ValidationError &,
                           const ValidationError &) = default;
};

struct ValidationResult {
    std::vector<ValidationError> errors;

    [[nodiscard]] bool ok() const { return errors.empty(); }
};

[[nodiscard]] const std::vector<FieldDescriptor> &fieldDescriptors();
[[nodiscard]] const std::vector<std::string_view> &allConfigKeys();
[[nodiscard]] Settings defaultSettings();
[[nodiscard]] Settings settingsFromValues(
    const std::map<std::string, std::string> &values);
[[nodiscard]] ValidationResult validate(const Settings &settings);
[[nodiscard]] bool enabled(const Settings &settings,
                           std::string_view key);
[[nodiscard]] std::string value(const Settings &settings,
                                std::string_view key);

} // namespace unilume::config_gui
