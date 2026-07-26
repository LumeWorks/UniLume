// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "engine_context.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace unilume::core {

enum class ShortcutScope : std::uint8_t {
    inherited,
    disabled,
    non_start,
    everywhere,
};

struct TypingConvenienceOptions {
    bool auto_capitalize{};
    bool double_space_to_period{};
    bool double_hyphen_to_em_dash{};
    ShortcutScope w_shortcut{ShortcutScope::inherited};
    ShortcutScope bracket_shortcut{ShortcutScope::inherited};

    friend bool operator==(
        const TypingConvenienceOptions &,
        const TypingConvenienceOptions &) = default;
};

// Ordered boundary pipeline around, but never inside, the inherited UniKey
// algorithm. One instance belongs to one input context.
class TypingPipeline {
public:
    explicit TypingPipeline(
        UlInputMethod method = UL_INPUT_METHOD_TELEX);

    KeyResult process(const KeyInput &input);
    void reset();
    void lineBreak();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);
    void setTypingOptions(const TypingConvenienceOptions &options);
    void setMacros(const macro::Snapshot &snapshot);
    void setKeymap(const keymap::Snapshot &snapshot);
    void setDictionary(const dictionary::Snapshot &snapshot);

    [[nodiscard]] const TypingConvenienceOptions &typingOptions() const;

private:
    [[nodiscard]] bool inactive() const;
    [[nodiscard]] bool shortcutAllowed(ShortcutScope scope) const;
    [[nodiscard]] bool tokenHasTelexVowel() const;
    [[nodiscard]] bool isLiteralContext() const;
    [[nodiscard]] KeyResult literal(std::string_view text);
    [[nodiscard]] KeyResult processShortcut(
        char base,
        char modifier,
        std::string_view fallback_text);
    [[nodiscard]] KeyResult combine(
        const KeyResult &first,
        const KeyResult &second,
        std::string_view fallback_text);
    void observe(std::string_view original_text);
    void clearTransientState();
    static bool eraseLastCharacter(std::string &text);

    EngineContext engine_;
    UlInputMethod method_{UL_INPUT_METHOD_TELEX};
    TypingConvenienceOptions options_;
    std::array<char, 2> transformed_input_{};
    std::string first_shortcut_output_;
    std::string combined_output_;
    std::string raw_token_;
    bool literal_context_{};
    bool previous_space_candidate_{};
    bool previous_hyphen_candidate_{};
    bool hyphen_pair_candidate_{};
    bool sentence_punctuation_{};
    bool capitalize_next_{};
};

} // namespace unilume::core
