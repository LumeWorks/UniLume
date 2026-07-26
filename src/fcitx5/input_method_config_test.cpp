// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_method_config.h"

#include <fcitx-config/rawconfig.h>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main()
{
    using namespace unilume::fcitx5;

    bool ok = true;
    InputMethodConfig input_method_config;
    fcitx::RawConfig raw;
    raw["InputMethod"] = "VNI";
    raw["OutputCharset"] = "UTF8";
    raw["SpellCheck"] = "False";
    raw["FreeMarking"] = "False";
    raw["ModernTone"] = "True";
    raw["AutoRestore"] = "False";
    ok &= expect(loadInputMethodConfig(input_method_config, raw),
                 "valid Fcitx configuration must load");
    ok &= expect(*input_method_config.input_method == ConfigInputMethod::VNI,
                 "Fcitx config must apply VNI");
    ok &= expect(*input_method_config.output_charset == ConfigOutputCharset::UTF8,
                 "Fcitx config must retain UTF8");
    ok &= expect(toUlInputMethod(*input_method_config.input_method) == UL_INPUT_METHOD_VNI,
                 "VNI must map to the UniLume context API");
    const unilume::config::Snapshot snapshot = snapshotFromConfig(input_method_config);
    ok &= expect(snapshot.input_method == unilume::config::InputMethod::vni &&
                     !snapshot.spell_check && !snapshot.free_marking &&
                     snapshot.modern_tone && !snapshot.auto_restore &&
                     !snapshot.macro_enabled,
                 "Fcitx config must produce the versioned UniLume snapshot");

    fcitx::RawConfig invalid_boolean;
    invalid_boolean["SpellCheck"] = "maybe";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_boolean),
                 "invalid boolean option must be rejected");
    ok &= expect(!*input_method_config.spell_check,
                 "invalid boolean must preserve active configuration");

    fcitx::RawConfig unknown_option;
    unknown_option["DecorativeOption"] = "True";
    ok &= expect(!loadInputMethodConfig(input_method_config, unknown_option),
                 "unknown Fcitx option must be rejected");

    fcitx::RawConfig invalid_method;
    invalid_method["InputMethod"] = "Unknown";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_method),
                 "unknown input method must be rejected");
    ok &= expect(*input_method_config.input_method == ConfigInputMethod::VNI,
                 "unknown input method must not replace active configuration");

    fcitx::RawConfig invalid_charset;
    invalid_charset["OutputCharset"] = "TCVN3";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_charset),
                 "unsupported charset must be rejected");
    ok &= expect(*input_method_config.output_charset == ConfigOutputCharset::UTF8,
                 "unsupported charset must not replace UTF8");

    fcitx::RawConfig description;
    input_method_config.dumpDescription(description);
    ok &= expect(description.hasSubItems(),
                 "config metadata must contain declared options");

    return ok ? 0 : 1;
}
