// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "unilume_context.h"
#include "config_snapshot.h"

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>

namespace unilume::fcitx5 {

// These identifiers are part of Fcitx's persisted per-input-method config.
// Keep them explicit and closed: accepting an arbitrary legacy charset would
// permit lossy commits, which UniLume intentionally does not support.
FCITX_CONFIG_ENUM(ConfigInputMethod, Telex, VNI, VIQR)
FCITX_CONFIG_ENUM(ConfigOutputCharset, UTF8)

FCITX_CONFIGURATION(
    InputMethodConfig,
    fcitx::Option<ConfigInputMethod> input_method{
        this, "InputMethod", "Vietnamese input method", ConfigInputMethod::Telex};
    fcitx::Option<ConfigOutputCharset> output_charset{
        this, "OutputCharset", "Output charset (lossless only)",
        ConfigOutputCharset::UTF8};
    fcitx::Option<bool> spell_check{this, "SpellCheck", "Spell check", true};
    fcitx::Option<bool> free_marking{this, "FreeMarking", "Free marking", true};
    fcitx::Option<bool> modern_tone{this, "ModernTone", "Modern tone placement", false};
    fcitx::Option<bool> auto_restore{this, "AutoRestore", "Restore non-Vietnamese words", true};
    fcitx::Option<bool> macro_enabled{
        this, "MacroEnabled", "Enable word-boundary macros", false};
    fcitx::Option<std::string> macro_file{
        this, "MacroFile", "Validated UTF-8 macro table", ""};
    fcitx::Option<bool> keymap_enabled{
        this, "KeymapEnabled", "Enable validated custom keymap", false};
    fcitx::Option<std::string> keymap_file{
        this, "KeymapFile", "Validated UniKey-compatible keymap", ""};
    fcitx::Option<bool> dictionary_enabled{
        this, "DictionaryEnabled", "Enable personal dictionary policy", false};
    fcitx::Option<std::string> dictionary_file{
        this, "DictionaryFile", "Validated personal dictionary", ""};
    fcitx::Option<bool> application_policy_enabled{
        this, "ApplicationPolicyEnabled",
        "Enable per-application input policy", false};
    fcitx::Option<std::string> application_policy_file{
        this, "ApplicationPolicyFile",
        "Validated per-application input policy", ""};
    fcitx::Option<std::string> cycle_mode_hotkey{
        this, "CycleModeHotkey", "Cycle application input mode",
        "Control+Alt+u"};
    fcitx::Option<std::string> automatic_mode_hotkey{
        this, "AutomaticModeHotkey", "Select automatic mode", ""};
    fcitx::Option<std::string> direct_mode_hotkey{
        this, "DirectModeHotkey", "Select direct mode", ""};
    fcitx::Option<std::string> safe_preedit_mode_hotkey{
        this, "SafePreeditModeHotkey", "Select safe preedit mode", ""};
    fcitx::Option<std::string> off_mode_hotkey{
        this, "OffModeHotkey", "Turn processing off for this context", ""};)

[[nodiscard]] UlInputMethod toUlInputMethod(ConfigInputMethod method);
[[nodiscard]] UlInputMethod toUlInputMethod(config::InputMethod method);
[[nodiscard]] config::Snapshot snapshotFromConfig(
    const InputMethodConfig &config);
[[nodiscard]] bool loadInputMethodConfig(InputMethodConfig &destination,
                                         const fcitx::RawConfig &source);
[[nodiscard]] bool validateInputMethodConfig(
    const fcitx::RawConfig &source);

} // namespace unilume::fcitx5
