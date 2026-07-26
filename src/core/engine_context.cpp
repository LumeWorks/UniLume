// SPDX-License-Identifier: GPL-2.0-or-later

#include "engine_context.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace unilume::core {
namespace {

bool isCompositionBoundary(unsigned char key)
{
    return key <= 0x7f &&
           (std::isspace(key) != 0 || std::ispunct(key) != 0);
}

} // namespace

EngineContext::EngineContext(UlInputMethod method)
{
    if (ul_engine_create(method, &context_) != UL_STATUS_OK) {
        throw std::runtime_error("cannot create UniLume engine context");
    }
    raw_token_.reserve(128);
}

EngineContext::~EngineContext()
{
    ul_engine_destroy(context_);
}

KeyResult EngineContext::process(const KeyInput &input)
{
    const std::uint64_t sequence = next_sequence_++;
    switch (input.kind) {
    case KeyKind::text:
        return processText(input, sequence);
    case KeyKind::backspace:
        return processBackspace(sequence);
    case KeyKind::reset:
    case KeyKind::navigation:
        reset();
        return {false, sequence, 0, {}, {}, true, false};
    }
    return makeFailure(sequence, false);
}

void EngineContext::reset()
{
    ul_engine_reset(context_);
    raw_token_.clear();
    displayed_token_characters_ = 0;
    literal_mode_ = false;
    line_literal_mode_ = false;
    quote_literal_mode_ = false;
}

void EngineContext::setInputMethod(UlInputMethod method)
{
    if (ul_engine_set_input_method(context_, method) != UL_STATUS_OK) {
        throw std::invalid_argument("unsupported UniLume input method");
    }
    reset();
}

void EngineContext::setOptions(const UlEngineOptions &options)
{
    if (ul_engine_set_options(context_, &options) != UL_STATUS_OK) {
        throw std::invalid_argument("invalid UniLume engine options");
    }
}

void EngineContext::setMacros(const macro::Snapshot &snapshot)
{
    std::vector<UlMacroEntry> entries;
    entries.reserve(snapshot.entries.size());
    for (const macro::Entry &entry : snapshot.entries) {
        entries.push_back(
            {entry.key.data(), entry.key.size(),
             entry.text.data(), entry.text.size()});
    }
    const UlMacroOptions options{
        snapshot.enabled ? 1 : 0,
        UL_MACRO_TRIGGER_WORD_BOUNDARY,
        UL_MACRO_CAPITALIZATION_EXACT,
    };
    const UlStatus status = ul_engine_set_macros(
        context_, entries.data(), entries.size(), &options);
    if (status != UL_STATUS_OK) {
        throw std::invalid_argument("invalid UniLume macro snapshot");
    }
    reset();
}

KeyResult EngineContext::processText(const KeyInput &input,
                                     std::uint64_t sequence)
{
    if (input.has_control_modifier || input.text.empty()) {
        reset();
        return {false, sequence, 0, {}, {}, true, false};
    }

    const auto first = static_cast<unsigned char>(input.text.front());
    if (input.text.size() != 1 || first > 0x7f) {
        if (literal_mode_) {
            return processLiteralText(input, sequence);
        }
        reset();
        return {true, sequence, 0, input.text, {}, true, false};
    }
    if (literal_mode_) {
        return processLiteralText(input, sequence);
    }
    if (std::isspace(first) != 0 &&
        shouldStartLineLiteral(raw_token_)) {
        return startLiteral(sequence, input.text, true);
    }
    if (first == '"' && !literal_mode_) {
        return startLiteral(sequence, input.text, false, true);
    }
    if (shouldStartTokenLiteral(static_cast<char>(first))) {
        return startLiteral(sequence, input.text, false);
    }

    UlEngineEdit edit{};
    const UlStatus status = ul_engine_process_ascii(
        context_,
        first,
        input.shift_pressed,
        input.caps_lock_on,
        output_.data(),
        output_.size(),
        &edit);
    if (status != UL_STATUS_OK) {
        return makeFailure(sequence, true);
    }
    if (!edit.handled) {
        output_[0] = static_cast<char>(first);
        edit.output_size = 1;
    }
    const bool reset_context = isCompositionBoundary(first);
    if (reset_context) {
        ul_engine_reset(context_);
    }
    if (std::isspace(first) != 0) {
        raw_token_.clear();
        displayed_token_characters_ = 0;
    } else {
        trackTokenEdit(
            edit.delete_before_cursor,
            std::string_view{output_.data(), edit.output_size},
            static_cast<char>(first));
    }
    return {
        true,
        sequence,
        edit.delete_before_cursor,
        std::string_view{output_.data(), edit.output_size},
        {},
        reset_context,
        false,
    };
}

KeyResult EngineContext::processBackspace(std::uint64_t sequence)
{
    UlEngineEdit edit{};
    const UlStatus status = ul_engine_backspace(
        context_, output_.data(), output_.size(), &edit);
    if (status != UL_STATUS_OK) {
        return makeFailure(sequence, false);
    }
    if (!edit.handled) {
        return {false, sequence, 0, {}, {}, false, false};
    }
    if (!raw_token_.empty()) {
        raw_token_.pop_back();
    }
    const auto delete_count =
        static_cast<std::size_t>(edit.delete_before_cursor);
    displayed_token_characters_ =
        displayed_token_characters_ > delete_count
            ? displayed_token_characters_ - delete_count
            : 0;
    displayed_token_characters_ +=
        utf8Characters(std::string_view{output_.data(), edit.output_size});
    return {
        true,
        sequence,
        edit.delete_before_cursor,
        std::string_view{output_.data(), edit.output_size},
        {},
        false,
        false,
    };
}

KeyResult EngineContext::makeFailure(std::uint64_t sequence, bool handled)
{
    reset();
    return {handled, sequence, 0, {}, {}, true, true};
}

KeyResult EngineContext::processLiteralText(
    const KeyInput &input,
    std::uint64_t sequence)
{
    if (quote_literal_mode_ && input.text == "\"") {
        quote_literal_mode_ = false;
        literal_mode_ = line_literal_mode_;
        return {true, sequence, 0, input.text, {}, false, false};
    }
    if (line_literal_mode_ || quote_literal_mode_) {
        return {true, sequence, 0, input.text, {}, false, false};
    }
    const bool boundary =
        input.text.size() == 1 &&
        std::isspace(static_cast<unsigned char>(input.text.front())) != 0;
    if (boundary) {
        reset();
    }
    return {true, sequence, 0, input.text, {}, boundary, false};
}

KeyResult EngineContext::startLiteral(
    std::uint64_t sequence,
    std::string_view trigger,
    bool line_mode,
    bool quote_mode)
{
    if (raw_token_.size() + trigger.size() > output_.size()) {
        return makeFailure(sequence, true);
    }
    std::copy(raw_token_.begin(), raw_token_.end(), output_.begin());
    std::copy(
        trigger.begin(),
        trigger.end(),
        output_.begin() + static_cast<std::ptrdiff_t>(raw_token_.size()));
    const std::size_t output_size = raw_token_.size() + trigger.size();
    const auto delete_count =
        static_cast<std::int32_t>(displayed_token_characters_);
    ul_engine_reset(context_);
    raw_token_.clear();
    displayed_token_characters_ = 0;
    literal_mode_ = true;
    line_literal_mode_ = line_mode;
    quote_literal_mode_ = quote_mode;
    return {
        true,
        sequence,
        delete_count,
        std::string_view{output_.data(), output_size},
        {},
        false,
        false,
    };
}

bool EngineContext::shouldStartTokenLiteral(char key) const
{
    if (key == '@' || key == '_' || key == '<' || key == '=' ||
        key == '&' || key == '|') {
        return !raw_token_.empty();
    }
    if (key == ':' &&
        (raw_token_ == "http" || raw_token_ == "https" ||
         raw_token_ == "localhost" || raw_token_.ends_with(':'))) {
        return true;
    }
    return key == '>' && raw_token_.ends_with('-');
}

bool EngineContext::shouldStartLineLiteral(std::string_view token)
{
    static constexpr std::array tokens{
        std::string_view{"if"},
        std::string_view{"for"},
        std::string_view{"while"},
        std::string_view{"switch"},
        std::string_view{"return"},
        std::string_view{"class"},
        std::string_view{"struct"},
        std::string_view{"namespace"},
        std::string_view{"npm"},
        std::string_view{"git"},
        std::string_view{"cmake"},
    };
    return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

void EngineContext::trackTokenEdit(
    std::int32_t delete_before_cursor,
    std::string_view commit_text,
    char raw_key)
{
    if (raw_token_.size() == output_capacity) {
        raw_token_.clear();
        displayed_token_characters_ = 0;
        return;
    }
    raw_token_.push_back(raw_key);
    const auto delete_count =
        static_cast<std::size_t>(delete_before_cursor);
    displayed_token_characters_ =
        displayed_token_characters_ > delete_count
            ? displayed_token_characters_ - delete_count
            : 0;
    displayed_token_characters_ += utf8Characters(commit_text);
}

std::size_t EngineContext::utf8Characters(std::string_view text)
{
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        count += (byte & 0xc0) != 0x80;
    }
    return count;
}

} // namespace unilume::core
