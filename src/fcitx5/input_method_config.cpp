// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_method_config.h"

#include <fcitx-utils/key.h>

#include <set>

namespace unilume::fcitx5 {
namespace {

bool isOneOf(const fcitx::RawConfig &source,
             const char *path,
             std::initializer_list<const char *> values)
{
    const std::string *value = source.valueByPath(path);
    if (!value) {
        return true;
    }
    for (const char *allowed : values) {
        if (*value == allowed) {
            return true;
        }
    }
    return false;
}

bool hasOnlyKnownOptions(const fcitx::RawConfig &source)
{
    for (const std::string &name : source.subItems()) {
        if (name != "InputMethod" && name != "OutputCharset" &&
            name != "SpellCheck" && name != "FreeMarking" &&
            name != "ModernTone" && name != "AutoRestore" &&
            name != "MacroEnabled" && name != "MacroFile" &&
            name != "KeymapEnabled" && name != "KeymapFile" &&
            name != "DictionaryEnabled" && name != "DictionaryFile" &&
            name != "VerifiedDirectEnabled" &&
            name != "ApplicationPolicyEnabled" &&
            name != "ApplicationPolicyFile" &&
            name != "CycleModeHotkey" &&
            name != "AutomaticModeHotkey" &&
            name != "DirectModeHotkey" &&
            name != "SafePreeditModeHotkey" &&
            name != "OffModeHotkey") {
            return false;
        }
    }
    return true;
}

bool validPath(const fcitx::RawConfig &source, const char *name)
{
    const std::string *path = source.valueByPath(name);
    return !path ||
           (path->find('\r') == std::string::npos &&
            path->find('\n') == std::string::npos &&
            path->find('\0') == std::string::npos);
}

bool validHotkeys(const fcitx::RawConfig &source)
{
    std::set<std::string> seen;
    for (const char *name :
         {"CycleModeHotkey", "AutomaticModeHotkey",
          "DirectModeHotkey", "SafePreeditModeHotkey",
          "OffModeHotkey"}) {
        const std::string *value = source.valueByPath(name);
        if (!value || value->empty()) {
            continue;
        }
        const fcitx::Key key(*value);
        if (!key.isValid() ||
            !seen.emplace(key.normalize().toString()).second) {
            return false;
        }
    }
    return true;
}

} // namespace

UlInputMethod toUlInputMethod(ConfigInputMethod method)
{
    switch (method) {
    case ConfigInputMethod::Telex:
        return UL_INPUT_METHOD_TELEX;
    case ConfigInputMethod::VNI:
        return UL_INPUT_METHOD_VNI;
    case ConfigInputMethod::VIQR:
        return UL_INPUT_METHOD_VIQR;
    }
    return UL_INPUT_METHOD_TELEX;
}

UlInputMethod toUlInputMethod(config::InputMethod method)
{
    switch (method) {
    case config::InputMethod::telex:
        return UL_INPUT_METHOD_TELEX;
    case config::InputMethod::vni:
        return UL_INPUT_METHOD_VNI;
    case config::InputMethod::viqr:
        return UL_INPUT_METHOD_VIQR;
    }
    return UL_INPUT_METHOD_TELEX;
}

config::Snapshot snapshotFromConfig(const InputMethodConfig &config)
{
    config::Snapshot snapshot = config::defaults();
    switch (*config.input_method) {
    case ConfigInputMethod::Telex:
        snapshot.input_method = config::InputMethod::telex;
        break;
    case ConfigInputMethod::VNI:
        snapshot.input_method = config::InputMethod::vni;
        break;
    case ConfigInputMethod::VIQR:
        snapshot.input_method = config::InputMethod::viqr;
        break;
    }
    snapshot.spell_check = *config.spell_check;
    snapshot.free_marking = *config.free_marking;
    snapshot.modern_tone = *config.modern_tone;
    snapshot.auto_restore = *config.auto_restore;
    snapshot.macro_enabled = *config.macro_enabled;
    snapshot.macro_file = *config.macro_file;
    return snapshot;
}

bool loadInputMethodConfig(InputMethodConfig &destination,
                           const fcitx::RawConfig &source)
{
    if (!validateInputMethodConfig(source)) {
        return false;
    }
    // Do not reset omitted fields: Fcitx may submit a partial update from its
    // configuration UI. Validation above makes every supplied field closed.
    destination.load(source, false);
    return true;
}

bool validateInputMethodConfig(const fcitx::RawConfig &source)
{
    if (!hasOnlyKnownOptions(source) ||
        !isOneOf(source, "InputMethod", {"Telex", "VNI", "VIQR"}) ||
        !isOneOf(source, "OutputCharset", {"UTF8"}) ||
        !isOneOf(source, "SpellCheck", {"True", "False"}) ||
        !isOneOf(source, "FreeMarking", {"True", "False"}) ||
        !isOneOf(source, "ModernTone", {"True", "False"}) ||
        !isOneOf(source, "AutoRestore", {"True", "False"}) ||
        !isOneOf(source, "MacroEnabled", {"True", "False"}) ||
        !isOneOf(source, "KeymapEnabled", {"True", "False"}) ||
        !isOneOf(source, "DictionaryEnabled", {"True", "False"}) ||
        !isOneOf(source, "VerifiedDirectEnabled", {"True", "False"}) ||
        !isOneOf(source, "ApplicationPolicyEnabled", {"True", "False"}) ||
        !validPath(source, "KeymapFile") ||
        !validPath(source, "DictionaryFile") ||
        !validPath(source, "ApplicationPolicyFile") ||
        !validHotkeys(source)) {
        return false;
    }
    if (const std::string *path = source.valueByPath("MacroFile")) {
        config::Snapshot snapshot = config::defaults();
        snapshot.macro_file = *path;
        if (!config::validate(snapshot).empty()) {
            return false;
        }
    }
    return true;
}

} // namespace unilume::fcitx5
