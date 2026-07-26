// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_method_config.h"

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
            name != "MacroEnabled" && name != "MacroFile") {
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
        !isOneOf(source, "MacroEnabled", {"True", "False"})) {
        return false;
    }
    if (const std::string *path = source.valueByPath("MacroFile")) {
        config::Snapshot snapshot = config::defaults();
        snapshot.macro_file = *path;
        return config::validate(snapshot).empty();
    }
    return true;
}

} // namespace unilume::fcitx5
