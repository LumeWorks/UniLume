// SPDX-License-Identifier: GPL-2.0-or-later

#include "typing_pipeline.h"

#include <cctype>
#include <limits>

namespace unilume::core {
namespace {

bool isAsciiWhitespace(std::string_view text)
{
    return text.size() == 1 &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0;
}

bool isAsciiLower(char value)
{
    return value >= 'a' && value <= 'z';
}

bool isAsciiUpper(char value)
{
    return value >= 'A' && value <= 'Z';
}

bool isBracketShortcut(char value)
{
    return value == '[' || value == ']' ||
           value == '{' || value == '}';
}

} // namespace

TypingPipeline::TypingPipeline(UlInputMethod method)
    : engine_(method), method_(method)
{
    first_shortcut_output_.reserve(32);
    combined_output_.reserve(64);
    raw_token_.reserve(128);
}

KeyResult TypingPipeline::process(const KeyInput &input)
{
    if (inactive()) {
        return engine_.process(input);
    }
    if (input.kind != KeyKind::text || input.has_control_modifier ||
        input.text.size() != 1 ||
        static_cast<unsigned char>(input.text.front()) > 0x7f) {
        if (input.kind != KeyKind::text || input.has_control_modifier) {
            clearTransientState();
        }
        return engine_.process(input);
    }

    const char original = input.text.front();
    const bool literal_before = isLiteralContext();
    const bool whitespace = isAsciiWhitespace(input.text);

    if (options_.double_space_to_period && whitespace &&
        previous_space_candidate_ && !literal_before) {
        KeyResult result = engine_.process(input);
        result.delete_before_cursor = 1;
        result.commit_text = ". ";
        result.reset_context = true;
        result.defer_preedit_commit = false;
        previous_space_candidate_ = false;
        sentence_punctuation_ = false;
        capitalize_next_ = options_.auto_capitalize;
        raw_token_.clear();
        return result;
    }

    if (options_.double_hyphen_to_em_dash && whitespace &&
        hyphen_pair_candidate_ && !literal_before) {
        KeyResult result = engine_.process(input);
        result.delete_before_cursor = 2;
        result.commit_text = "— ";
        result.reset_context = true;
        result.defer_preedit_commit = false;
        previous_hyphen_candidate_ = false;
        hyphen_pair_candidate_ = false;
        raw_token_.clear();
        return result;
    }

    KeyInput transformed = input;
    if (options_.auto_capitalize && capitalize_next_ &&
        isAsciiLower(original) && !literal_before) {
        transformed_input_[0] =
            static_cast<char>(original - ('a' - 'A'));
        transformed.text =
            std::string_view{transformed_input_.data(), 1};
        capitalize_next_ = false;
    } else if (!whitespace && !isAsciiLower(original) &&
               !isAsciiUpper(original)) {
        capitalize_next_ = false;
    }

    const char effective = transformed.text.front();
    const bool uppercase = isAsciiUpper(effective) ||
                           effective == '{' || effective == '}';

    KeyResult result;
    if ((effective == 'w' || effective == 'W') &&
        options_.w_shortcut != ShortcutScope::inherited &&
        !literal_before && !tokenHasTelexVowel()) {
        if (!shortcutAllowed(options_.w_shortcut)) {
            result = literal(transformed.text);
        } else if (method_ == UL_INPUT_METHOD_TELEX) {
            result = engine_.process(transformed);
        } else {
            const char modifier =
                method_ == UL_INPUT_METHOD_VNI ? '7' : '+';
            result = processShortcut(
                uppercase ? 'U' : 'u', modifier, transformed.text);
        }
    } else if (isBracketShortcut(effective) &&
               options_.bracket_shortcut !=
                   ShortcutScope::inherited &&
               !literal_before) {
        if (!shortcutAllowed(options_.bracket_shortcut)) {
            result = literal(transformed.text);
        } else {
            const bool u_horn = effective == ']' || effective == '}';
            const char base = u_horn
                                  ? (uppercase ? 'U' : 'u')
                                  : (uppercase ? 'O' : 'o');
            const char modifier =
                method_ == UL_INPUT_METHOD_TELEX
                    ? 'w'
                    : (method_ == UL_INPUT_METHOD_VNI ? '7' : '+');
            result = processShortcut(base, modifier, transformed.text);
        }
    } else {
        result = engine_.process(transformed);
    }

    if (previous_space_candidate_ && !whitespace) {
        result.commit_preedit_before = true;
    }
    if (options_.double_space_to_period && whitespace &&
        !literal_before && !raw_token_.empty()) {
        result.defer_preedit_commit = true;
    }

    previous_space_candidate_ =
        options_.double_space_to_period && whitespace &&
        !literal_before && !raw_token_.empty();
    if (options_.double_hyphen_to_em_dash && original == '-' &&
        !literal_before) {
        if (previous_hyphen_candidate_) {
            hyphen_pair_candidate_ = true;
            previous_hyphen_candidate_ = false;
        } else {
            previous_hyphen_candidate_ = !raw_token_.empty();
        }
    } else {
        previous_hyphen_candidate_ = false;
        if (!whitespace) {
            hyphen_pair_candidate_ = false;
        }
    }

    if (options_.auto_capitalize && !literal_before) {
        if (original == '.' || original == '!' || original == '?') {
            sentence_punctuation_ = true;
        } else if (whitespace && sentence_punctuation_) {
            sentence_punctuation_ = false;
            capitalize_next_ = true;
        } else if (!whitespace) {
            sentence_punctuation_ = false;
        }
    }

    observe(input.text);
    return result;
}

void TypingPipeline::reset()
{
    engine_.reset();
    clearTransientState();
}

void TypingPipeline::lineBreak()
{
    engine_.reset();
    clearTransientState();
    capitalize_next_ = options_.auto_capitalize;
}

void TypingPipeline::setInputMethod(UlInputMethod method)
{
    engine_.setInputMethod(method);
    method_ = method;
    clearTransientState();
}

void TypingPipeline::setOptions(const UlEngineOptions &options)
{
    engine_.setOptions(options);
    clearTransientState();
}

void TypingPipeline::setTypingOptions(
    const TypingConvenienceOptions &options)
{
    if (options == options_) {
        return;
    }
    engine_.reset();
    options_ = options;
    clearTransientState();
}

void TypingPipeline::setMacros(const macro::Snapshot &snapshot)
{
    engine_.setMacros(snapshot);
    clearTransientState();
}

void TypingPipeline::setKeymap(const keymap::Snapshot &snapshot)
{
    engine_.setKeymap(snapshot);
    clearTransientState();
}

void TypingPipeline::setDictionary(
    const dictionary::Snapshot &snapshot)
{
    engine_.setDictionary(snapshot);
    clearTransientState();
}

const TypingConvenienceOptions &TypingPipeline::typingOptions() const
{
    return options_;
}

bool TypingPipeline::inactive() const
{
    return !options_.auto_capitalize &&
           !options_.double_space_to_period &&
           !options_.double_hyphen_to_em_dash &&
           options_.w_shortcut == ShortcutScope::inherited &&
           options_.bracket_shortcut == ShortcutScope::inherited;
}

bool TypingPipeline::shortcutAllowed(ShortcutScope scope) const
{
    return scope == ShortcutScope::everywhere ||
           (scope == ShortcutScope::non_start && !raw_token_.empty());
}

bool TypingPipeline::tokenHasTelexVowel() const
{
    return raw_token_.find_first_of("aAeEiIoOuUyY") !=
           std::string::npos;
}

bool TypingPipeline::isLiteralContext() const
{
    return literal_context_;
}

KeyResult TypingPipeline::literal(std::string_view text)
{
    const KeyResult reset_result =
        engine_.process({KeyKind::reset, {}, false, false, false});
    raw_token_.clear();
    literal_context_ = false;
    return {
        true,
        reset_result.sequence_id,
        0,
        text,
        {},
        true,
        false,
    };
}

KeyResult TypingPipeline::processShortcut(
    char base,
    char modifier,
    std::string_view fallback_text)
{
    transformed_input_[0] = base;
    transformed_input_[1] = modifier;
    const KeyResult first = engine_.process({
        KeyKind::text,
        std::string_view{transformed_input_.data(), 1},
        false,
        false,
        false,
    });
    first_shortcut_output_.assign(first.commit_text);
    KeyResult stable_first = first;
    stable_first.commit_text = first_shortcut_output_;
    const KeyResult second = engine_.process({
        KeyKind::text,
        std::string_view{transformed_input_.data() + 1, 1},
        false,
        false,
        false,
    });
    return combine(stable_first, second, fallback_text);
}

KeyResult TypingPipeline::combine(
    const KeyResult &first,
    const KeyResult &second,
    std::string_view fallback_text)
{
    if (!first.handled || !second.handled ||
        first.require_fallback || second.require_fallback) {
        engine_.reset();
        return {
            true,
            second.sequence_id,
            0,
            fallback_text,
            {},
            true,
            true,
        };
    }

    combined_output_.clear();
    std::size_t external_delete = 0;
    const auto apply = [&](const KeyResult &edit) {
        std::size_t remaining =
            static_cast<std::size_t>(edit.delete_before_cursor);
        while (remaining != 0 && eraseLastCharacter(combined_output_)) {
            --remaining;
        }
        external_delete += remaining;
        combined_output_.append(edit.commit_text);
    };
    apply(first);
    apply(second);
    if (external_delete >
        static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max())) {
        engine_.reset();
        return {
            true,
            second.sequence_id,
            0,
            fallback_text,
            {},
            true,
            true,
        };
    }
    return {
        true,
        second.sequence_id,
        static_cast<std::int32_t>(external_delete),
        combined_output_,
        {},
        second.reset_context,
        false,
    };
}

void TypingPipeline::observe(std::string_view original_text)
{
    if (original_text.size() != 1) {
        raw_token_.clear();
        literal_context_ = false;
        return;
    }
    const unsigned char value =
        static_cast<unsigned char>(original_text.front());
    if (std::isspace(value) != 0) {
        raw_token_.clear();
        literal_context_ = false;
        return;
    }
    if (value == '@' || value == '_' || value == '/' ||
        value == '\\' || value == ':' || value == '=' ||
        value == '<' || value == '>' || value == '&' ||
        value == '|' || value == '"' || value == '\'') {
        literal_context_ = true;
    }
    if (std::isalnum(value) != 0 || value == '.' || value == '-' ||
        value == '+' || value == '@' || value == '_') {
        if (raw_token_.size() < 128) {
            raw_token_.push_back(static_cast<char>(value));
        } else {
            raw_token_.clear();
            literal_context_ = true;
        }
    } else {
        raw_token_.clear();
    }
}

void TypingPipeline::clearTransientState()
{
    raw_token_.clear();
    literal_context_ = false;
    previous_space_candidate_ = false;
    previous_hyphen_candidate_ = false;
    hyphen_pair_candidate_ = false;
    sentence_punctuation_ = false;
    capitalize_next_ = false;
}

bool TypingPipeline::eraseLastCharacter(std::string &text)
{
    if (text.empty()) {
        return false;
    }
    std::size_t position = text.size() - 1;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    text.erase(position);
    return true;
}

} // namespace unilume::core
