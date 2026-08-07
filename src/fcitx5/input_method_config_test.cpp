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
    ok &= expect(*input_method_config.verified_direct_enabled,
                 "zero-preedit replacement must be enabled by default");
    fcitx::RawConfig raw;
    raw["InputMethod"] = "VNI";
    raw["OutputCharset"] = "UTF8";
    raw["SpellCheck"] = "False";
    raw["FreeMarking"] = "False";
    raw["ModernTone"] = "True";
    raw["AutoRestore"] = "False";
    raw["AutoCapitalize"] = "True";
    raw["DoubleSpaceToPeriod"] = "True";
    raw["DoubleHyphenToEmDash"] = "True";
    raw["WShortcut"] = "NonStart";
    raw["BracketShortcut"] = "Everywhere";
    raw["MacroEnabled"] = "False";
    raw["MacroFile"] = "/tmp/unilume-macros";
    raw["KeymapEnabled"] = "True";
    raw["KeymapFile"] = "/tmp/unilume-keymap";
    raw["DictionaryEnabled"] = "True";
    raw["DictionaryFile"] = "/tmp/unilume-dictionary";
    raw["VerifiedDirectEnabled"] = "True";
    raw["DirectStrategy"] = "Guarded";
    raw["ApplicationPolicyEnabled"] = "True";
    raw["ApplicationPolicyFile"] = "/tmp/unilume-application-policy";
    raw["CycleModeHotkey"] = "Control+Alt+u";
    raw["AutomaticModeHotkey"] = "Control+Alt+a";
    raw["EmojiEnabled"] = "True";
    raw["EmojiHotkey"] = "Control+Alt+period";
    ok &= expect(loadInputMethodConfig(input_method_config, raw),
                 "valid Fcitx configuration must load");
    ok &= expect(*input_method_config.input_method == ConfigInputMethod::VNI,
                 "Fcitx config must apply VNI");
    ok &= expect(*input_method_config.application_policy_enabled &&
                     *input_method_config.application_policy_file ==
                         "/tmp/unilume-application-policy",
                 "application policy configuration must load");
    ok &= expect(*input_method_config.verified_direct_enabled,
                 "verified direct feature flag must load");
    ok &= expect(*input_method_config.direct_strategy ==
                     ConfigDirectStrategy::Guarded &&
                     toDirectStrategy(*input_method_config.direct_strategy) ==
                     DirectStrategy::guarded,
                 "Guarded direct strategy must load and map exactly");
    ok &= expect(*input_method_config.emoji_enabled &&
                     *input_method_config.emoji_hotkey ==
                         "Control+Alt+period",
                 "optional emoji configuration must load");
    const unilume::core::TypingConvenienceOptions typing =
        typingOptionsFromConfig(input_method_config);
    ok &= expect(
        typing.auto_capitalize && typing.double_space_to_period &&
            typing.double_hyphen_to_em_dash &&
            typing.w_shortcut ==
                unilume::core::ShortcutScope::non_start &&
            typing.bracket_shortcut ==
                unilume::core::ShortcutScope::everywhere,
        "typing pipeline configuration must map every option");
    ok &= expect(*input_method_config.output_charset == ConfigOutputCharset::UTF8,
                 "Fcitx config must retain UTF8");
    ok &= expect(toUlInputMethod(*input_method_config.input_method) == UL_INPUT_METHOD_VNI,
                 "VNI must map to the UniLume context API");
    const unilume::config::Snapshot snapshot = snapshotFromConfig(input_method_config);
    ok &= expect(snapshot.input_method == unilume::config::InputMethod::vni &&
                     !snapshot.spell_check && !snapshot.free_marking &&
                     snapshot.modern_tone && !snapshot.auto_restore &&
                     !snapshot.macro_enabled &&
                     snapshot.macro_file == "/tmp/unilume-macros",
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

    fcitx::RawConfig invalid_scope;
    invalid_scope["WShortcut"] = "Sometimes";
    ok &= expect(
        !loadInputMethodConfig(input_method_config, invalid_scope),
        "unknown shortcut scope must be rejected");

    fcitx::RawConfig invalid_charset;
    invalid_charset["OutputCharset"] = "TCVN3";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_charset),
                 "unsupported charset must be rejected");
    ok &= expect(*input_method_config.output_charset == ConfigOutputCharset::UTF8,
                 "unsupported charset must not replace UTF8");

    fcitx::RawConfig invalid_strategy;
    invalid_strategy["DirectStrategy"] = "Smooth";
    ok &= expect(!loadInputMethodConfig(input_method_config,
                                        invalid_strategy),
                 "unknown direct strategy must be rejected");
    ok &= expect(*input_method_config.direct_strategy ==
                     ConfigDirectStrategy::Guarded,
                 "invalid strategy must preserve active configuration");

    fcitx::RawConfig invalid_macro_path;
    invalid_macro_path["MacroFile"] = "bad\npath";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_macro_path),
                 "invalid macro path must be rejected");

    fcitx::RawConfig invalid_keymap_path;
    invalid_keymap_path["KeymapFile"] = "bad\npath";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_keymap_path),
                 "invalid keymap path must be rejected");
    ok &= expect(*input_method_config.keymap_enabled &&
                     *input_method_config.keymap_file ==
                         "/tmp/unilume-keymap",
                 "invalid keymap update must preserve active configuration");

    fcitx::RawConfig invalid_dictionary_path;
    invalid_dictionary_path["DictionaryFile"] = "bad\npath";
    ok &= expect(
        !loadInputMethodConfig(input_method_config, invalid_dictionary_path),
        "invalid dictionary path must be rejected");
    ok &= expect(*input_method_config.dictionary_enabled &&
                     *input_method_config.dictionary_file ==
                         "/tmp/unilume-dictionary",
                 "invalid dictionary update must preserve configuration");

    fcitx::RawConfig invalid_policy_path;
    invalid_policy_path["ApplicationPolicyFile"] = "bad\npath";
    ok &= expect(
        !loadInputMethodConfig(input_method_config, invalid_policy_path),
        "invalid application policy path must be rejected");

    fcitx::RawConfig invalid_hotkey;
    invalid_hotkey["DirectModeHotkey"] = "Not-A-Real-Fcitx-Key";
    ok &= expect(!loadInputMethodConfig(input_method_config, invalid_hotkey),
                 "invalid mode hotkey must be rejected");

    fcitx::RawConfig conflicting_hotkeys;
    conflicting_hotkeys["DirectModeHotkey"] = "Control+Alt+d";
    conflicting_hotkeys["OffModeHotkey"] = "Control+Alt+d";
    ok &= expect(
        !loadInputMethodConfig(input_method_config, conflicting_hotkeys),
        "conflicting mode hotkeys must be rejected");

    fcitx::RawConfig emoji_hotkey_conflict;
    emoji_hotkey_conflict["CycleModeHotkey"] = "Control+Alt+u";
    emoji_hotkey_conflict["EmojiHotkey"] = "Control+Alt+u";
    ok &= expect(
        !loadInputMethodConfig(input_method_config, emoji_hotkey_conflict),
        "emoji hotkey must not conflict with mode hotkeys");

    fcitx::RawConfig description;
    input_method_config.dumpDescription(description);
    ok &= expect(description.hasSubItems(),
                 "config metadata must contain declared options");

    return ok ? 0 : 1;
}
