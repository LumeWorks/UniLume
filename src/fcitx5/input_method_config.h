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
    fcitx::Option<bool> auto_restore{this, "AutoRestore", "Restore non-Vietnamese words", true};)

[[nodiscard]] UlInputMethod toUlInputMethod(ConfigInputMethod method);
[[nodiscard]] config::Snapshot snapshotFromConfig(
    const InputMethodConfig &config);
[[nodiscard]] bool loadInputMethodConfig(InputMethodConfig &destination,
                                         const fcitx::RawConfig &source);

} // namespace unilume::fcitx5
