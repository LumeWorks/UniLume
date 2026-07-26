// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "unilume_context.h"
#include "config_snapshot.h"
#include "typing_pipeline.h"

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-utils/i18n.h>

namespace unilume::fcitx5 {

// These identifiers are part of Fcitx's persisted per-input-method config.
// Keep them explicit and closed: accepting an arbitrary legacy charset would
// permit lossy commits, which UniLume intentionally does not support.
FCITX_CONFIG_ENUM(ConfigInputMethod, Telex, VNI, VIQR)
FCITX_CONFIG_ENUM(ConfigOutputCharset, UTF8)
enum class ConfigShortcutScope {
    Inherited,
    Disabled,
    NonStart,
    Everywhere,
};
FCITX_CONFIG_ENUM_NAME_WITH_I18N(
    ConfigShortcutScope,
    N_("Inherited"), N_("Disabled"), N_("NonStart"), N_("Everywhere"))

using ShortcutScopeOption = fcitx::Option<
    ConfigShortcutScope,
    fcitx::NoConstrain<ConfigShortcutScope>,
    fcitx::DefaultMarshaller<ConfigShortcutScope>,
    ConfigShortcutScopeI18NAnnotation>;

FCITX_CONFIGURATION(
    InputMethodConfig,
    fcitx::Option<ConfigInputMethod> input_method{
        this, "InputMethod", _("Vietnamese input method"), ConfigInputMethod::Telex};
    fcitx::Option<ConfigOutputCharset> output_charset{
        this, "OutputCharset", _("Output charset (lossless only)"),
        ConfigOutputCharset::UTF8};
    fcitx::Option<bool> spell_check{this, "SpellCheck", _("Spell check"), true};
    fcitx::Option<bool> free_marking{this, "FreeMarking", _("Free marking"), true};
    fcitx::Option<bool> modern_tone{this, "ModernTone", _("Modern tone placement"), false};
    fcitx::Option<bool> auto_restore{this, "AutoRestore", _("Restore non-Vietnamese words"), true};
    fcitx::Option<bool> auto_capitalize{
        this, "AutoCapitalize",
        _("Capitalize after sentence punctuation or Enter"), false};
    fcitx::Option<bool> double_space_to_period{
        this, "DoubleSpaceToPeriod", _("Replace word-ending double space with period"), false};
    fcitx::Option<bool> double_hyphen_to_em_dash{
        this, "DoubleHyphenToEmDash", _("Replace prose double hyphen with em dash"), false};
    ShortcutScopeOption w_shortcut{
        this, "WShortcut", _("Standalone w to u-horn shortcut scope"),
        ConfigShortcutScope::Inherited};
    ShortcutScopeOption bracket_shortcut{
        this, "BracketShortcut", _("Bracket to horn-vowel shortcut scope"),
        ConfigShortcutScope::Inherited};
    fcitx::Option<bool> macro_enabled{
        this, "MacroEnabled", _("Enable word-boundary macros"), false};
    fcitx::Option<std::string> macro_file{
        this, "MacroFile", _("Validated UTF-8 macro table"), ""};
    fcitx::Option<bool> keymap_enabled{
        this, "KeymapEnabled", _("Enable validated custom keymap"), false};
    fcitx::Option<std::string> keymap_file{
        this, "KeymapFile", _("Validated UniKey-compatible keymap"), ""};
    fcitx::Option<bool> dictionary_enabled{
        this, "DictionaryEnabled", _("Enable personal dictionary policy"), false};
    fcitx::Option<std::string> dictionary_file{
        this, "DictionaryFile", _("Validated personal dictionary"), ""};
    fcitx::Option<bool> verified_direct_enabled{
        this, "VerifiedDirectEnabled",
        _("Enable capability-gated verified direct replacement"), false};
    fcitx::Option<bool> application_policy_enabled{
        this, "ApplicationPolicyEnabled",
        _("Enable per-application input policy"), false};
    fcitx::Option<std::string> application_policy_file{
        this, "ApplicationPolicyFile",
        _("Validated per-application input policy"), ""};
    fcitx::Option<std::string> cycle_mode_hotkey{
        this, "CycleModeHotkey", _("Cycle application input mode"),
        "Control+Alt+u"};
    fcitx::Option<std::string> automatic_mode_hotkey{
        this, "AutomaticModeHotkey", _("Select automatic mode"), ""};
    fcitx::Option<std::string> direct_mode_hotkey{
        this, "DirectModeHotkey", _("Select direct mode"), ""};
    fcitx::Option<std::string> safe_preedit_mode_hotkey{
        this, "SafePreeditModeHotkey", _("Select safe preedit mode"), ""};
    fcitx::Option<std::string> off_mode_hotkey{
        this, "OffModeHotkey", _("Turn processing off for this context"), ""};
    fcitx::Option<bool> emoji_enabled{
        this, "EmojiEnabled", _("Enable the optional emoji picker"), false};
    fcitx::Option<std::string> emoji_hotkey{
        this, "EmojiHotkey", _("Open the emoji picker"),
        "Control+Alt+period"};)

[[nodiscard]] UlInputMethod toUlInputMethod(ConfigInputMethod method);
[[nodiscard]] UlInputMethod toUlInputMethod(config::InputMethod method);
[[nodiscard]] config::Snapshot snapshotFromConfig(
    const InputMethodConfig &config);
[[nodiscard]] core::TypingConvenienceOptions typingOptionsFromConfig(
    const InputMethodConfig &config);
[[nodiscard]] bool loadInputMethodConfig(InputMethodConfig &destination,
                                         const fcitx::RawConfig &source);
[[nodiscard]] bool validateInputMethodConfig(
    const fcitx::RawConfig &source);

} // namespace unilume::fcitx5
