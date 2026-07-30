// SPDX-License-Identifier: GPL-2.0-or-later

#include "settings_model.h"

#include "application_policy.h"
#include "dictionary_contract.h"
#include "input_method_config.h"
#include "keymap_contract.h"
#include "macro_contract.h"

#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>

#include <set>

namespace unilume::config_gui {
namespace {

using ChoiceList = std::vector<std::string_view>;

const std::vector<FieldDescriptor> descriptors{
    {"InputMethod", N_("Vietnamese input method"), Category::general,
     FieldKind::choice, ChoiceList{"Telex", "VNI", "VIQR"}},
    {"OutputCharset", N_("Output charset"), Category::general,
     FieldKind::choice, ChoiceList{"UTF8"}},
    {"VerifiedDirectEnabled", N_("Verified direct replacement"),
     Category::general, FieldKind::boolean, {}},
    {"DirectStrategy", N_("Split-transport direct strategy"),
     Category::general, FieldKind::choice, ChoiceList{"Fast", "Guarded"}},
    {"SpellCheck", N_("Spell check"), Category::typing,
     FieldKind::boolean, {}},
    {"FreeMarking", N_("Free marking"), Category::typing,
     FieldKind::boolean, {}},
    {"ModernTone", N_("Modern tone placement"), Category::typing,
     FieldKind::boolean, {}},
    {"AutoRestore", N_("Restore non-Vietnamese words"), Category::typing,
     FieldKind::boolean, {}},
    {"AutoCapitalize", N_("Capitalize after sentence punctuation or Enter"),
     Category::typing, FieldKind::boolean, {}},
    {"DoubleSpaceToPeriod",
     N_("Replace word-ending double space with period"),
     Category::typing, FieldKind::boolean, {}},
    {"DoubleHyphenToEmDash",
     N_("Replace prose double hyphen with em dash"),
     Category::typing, FieldKind::boolean, {}},
    {"WShortcut", N_("Standalone w shortcut scope"), Category::typing,
     FieldKind::choice,
     ChoiceList{"Inherited", "Disabled", "NonStart", "Everywhere"}},
    {"BracketShortcut", N_("Bracket shortcut scope"), Category::typing,
     FieldKind::choice,
     ChoiceList{"Inherited", "Disabled", "NonStart", "Everywhere"}},
    {"ApplicationPolicyEnabled", N_("Enable per-application policy"),
     Category::applications, FieldKind::boolean, {}},
    {"ApplicationPolicyFile", N_("Managed application policy"),
     Category::applications, FieldKind::managed_path, {}},
    {"MacroEnabled", N_("Enable word-boundary macros"), Category::macros,
     FieldKind::boolean, {}},
    {"MacroFile", N_("Managed macro table"), Category::macros,
     FieldKind::managed_path, {}},
    {"DictionaryEnabled", N_("Enable personal dictionary"),
     Category::dictionary, FieldKind::boolean, {}},
    {"DictionaryFile", N_("Managed personal dictionary"),
     Category::dictionary, FieldKind::managed_path, {}},
    {"KeymapEnabled", N_("Enable custom keymap"), Category::keymap,
     FieldKind::boolean, {}},
    {"KeymapFile", N_("Managed custom keymap"), Category::keymap,
     FieldKind::managed_path, {}},
    {"CycleModeHotkey", N_("Cycle application input mode"),
     Category::shortcuts, FieldKind::hotkey, {}},
    {"AutomaticModeHotkey", N_("Select automatic mode"),
     Category::shortcuts, FieldKind::hotkey, {}},
    {"DirectModeHotkey", N_("Select direct mode"),
     Category::shortcuts, FieldKind::hotkey, {}},
    {"SafePreeditModeHotkey", N_("Select safe preedit mode"),
     Category::shortcuts, FieldKind::hotkey, {}},
    {"OffModeHotkey", N_("Turn processing off for this context"),
     Category::shortcuts, FieldKind::hotkey, {}},
    {"EmojiEnabled", N_("Enable the optional emoji picker"),
     Category::shortcuts, FieldKind::boolean, {}},
    {"EmojiHotkey", N_("Open the emoji picker"),
     Category::shortcuts, FieldKind::hotkey, {}},
};

const std::vector<std::string_view> keys = [] {
    std::vector<std::string_view> result;
    result.reserve(descriptors.size());
    for (const FieldDescriptor &descriptor : descriptors) {
        result.push_back(descriptor.key);
    }
    return result;
}();

bool contains(const ChoiceList &choices, std::string_view candidate)
{
    for (const std::string_view choice : choices) {
        if (choice == candidate) {
            return true;
        }
    }
    return false;
}

fcitx::RawConfig toRawConfig(const Settings &settings)
{
    fcitx::RawConfig raw;
    for (const auto &[key, item] : settings.values) {
        raw[key] = item;
    }
    return raw;
}

void addResourceError(ValidationResult &result,
                      std::string field,
                      std::size_t line,
                      std::string detail,
                      std::string message)
{
    if (line != 0) {
        message += " (line " + std::to_string(line);
        if (!detail.empty()) {
            message += ", " + detail;
        }
        message += ')';
    }
    result.errors.push_back({std::move(field), std::move(message)});
}

} // namespace

const std::vector<FieldDescriptor> &fieldDescriptors()
{
    return descriptors;
}

const std::vector<std::string_view> &allConfigKeys()
{
    return keys;
}

Settings defaultSettings()
{
    fcitx5::InputMethodConfig configuration;
    fcitx::RawConfig raw;
    configuration.save(raw);

    Settings settings;
    for (const std::string_view key : keys) {
        if (const std::string *item =
                raw.valueByPath(std::string(key))) {
            settings.values.emplace(key, *item);
        }
    }
    settings.resources.macros = macro::encode({});
    settings.resources.application_policy =
        policy::encode({});
    return settings;
}

Settings settingsFromValues(
    const std::map<std::string, std::string> &values)
{
    Settings result = defaultSettings();
    for (const std::string_view key : keys) {
        const auto item = values.find(std::string(key));
        if (item != values.end()) {
            result.values[item->first] = item->second;
        }
    }
    return result;
}

ValidationResult validate(const Settings &settings)
{
    ValidationResult result;
    for (const FieldDescriptor &descriptor : descriptors) {
        const auto item = settings.values.find(std::string(descriptor.key));
        if (item == settings.values.end()) {
            result.errors.push_back(
                {std::string(descriptor.key), "missing configuration field"});
            continue;
        }
        const std::string &candidate = item->second;
        switch (descriptor.kind) {
        case FieldKind::boolean:
            if (candidate != "True" && candidate != "False") {
                result.errors.push_back(
                    {std::string(descriptor.key),
                     "value must be True or False"});
            }
            break;
        case FieldKind::choice:
            if (!contains(descriptor.choices, candidate)) {
                result.errors.push_back(
                    {std::string(descriptor.key),
                     "value is outside the supported choices"});
            }
            break;
        case FieldKind::hotkey:
        case FieldKind::managed_path:
            if (candidate.find_first_of("\r\n") != std::string::npos ||
                candidate.find('\0') != std::string::npos) {
                result.errors.push_back(
                    {std::string(descriptor.key),
                     "value contains a forbidden control character"});
            }
            break;
        }
    }

    std::set<std::string> hotkeys;
    for (const FieldDescriptor &descriptor : descriptors) {
        if (descriptor.kind != FieldKind::hotkey) {
            continue;
        }
        const std::string candidate = value(settings, descriptor.key);
        if (candidate.empty()) {
            continue;
        }
        const fcitx::Key key(candidate);
        if (!key.isValid()) {
            result.errors.push_back(
                {std::string(descriptor.key), "invalid Fcitx hotkey"});
            continue;
        }
        if (!hotkeys.emplace(key.normalize().toString()).second) {
            result.errors.push_back(
                {std::string(descriptor.key),
                 "hotkey conflicts with another configured action"});
        }
    }

    if (!settings.resources.macros.empty()) {
        const macro::DecodeResult decoded =
            macro::decode(settings.resources.macros);
        if (!decoded.ok()) {
            addResourceError(result, "MacroFile", 0, {},
                             decoded.error);
        }
    } else if (enabled(settings, "MacroEnabled")) {
        result.errors.push_back(
            {"MacroFile", "enabled macro table cannot be empty"});
    }

    if (!settings.resources.dictionary.empty()) {
        const dictionary::DecodeResult decoded =
            dictionary::decode(settings.resources.dictionary);
        if (!decoded.ok()) {
            addResourceError(result, "DictionaryFile", decoded.line,
                             decoded.field, decoded.error);
        }
    } else if (enabled(settings, "DictionaryEnabled")) {
        result.errors.push_back(
            {"DictionaryFile", "enabled dictionary cannot be empty"});
    }

    if (!settings.resources.keymap.empty()) {
        const keymap::DecodeResult decoded =
            keymap::decode(settings.resources.keymap);
        if (!decoded.ok()) {
            addResourceError(result, "KeymapFile", decoded.line,
                             decoded.field, decoded.error);
        }
    } else if (enabled(settings, "KeymapEnabled")) {
        result.errors.push_back(
            {"KeymapFile", "enabled keymap cannot be empty"});
    }

    if (!settings.resources.application_policy.empty()) {
        const policy::DecodeResult decoded =
            policy::decode(settings.resources.application_policy);
        if (!decoded.ok()) {
            addResourceError(result, "ApplicationPolicyFile",
                             decoded.line, decoded.field, decoded.error);
        }
    } else if (enabled(settings, "ApplicationPolicyEnabled")) {
        result.errors.push_back(
            {"ApplicationPolicyFile",
             "enabled application policy cannot be empty"});
    }

    if (result.ok() &&
        !fcitx5::validateInputMethodConfig(toRawConfig(settings))) {
        result.errors.push_back(
            {"configuration", "configuration snapshot was rejected"});
    }
    return result;
}

bool enabled(const Settings &settings, std::string_view key)
{
    return value(settings, key) == "True";
}

std::string value(const Settings &settings, std::string_view key)
{
    const auto item = settings.values.find(std::string(key));
    return item == settings.values.end() ? std::string{} : item->second;
}

} // namespace unilume::config_gui
