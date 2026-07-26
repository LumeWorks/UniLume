// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "key_input.h"
#include "key_result.h"
#include "unilume_context.h"

#include <array>
#include <cstdint>
#include <string>

namespace unilume::core {

class EngineContext {
public:
    explicit EngineContext(UlInputMethod method = UL_INPUT_METHOD_TELEX);
    ~EngineContext();

    EngineContext(const EngineContext &) = delete;
    EngineContext &operator=(const EngineContext &) = delete;

    KeyResult process(const KeyInput &input);
    void reset();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);

private:
    KeyResult processText(const KeyInput &input, std::uint64_t sequence);
    KeyResult processBackspace(std::uint64_t sequence);
    KeyResult makeFailure(std::uint64_t sequence, bool handled);
    KeyResult processLiteralText(const KeyInput &input,
                                 std::uint64_t sequence);
    KeyResult startLiteral(std::uint64_t sequence,
                           std::string_view trigger,
                           bool line_mode,
                           bool quote_mode = false);
    void trackTokenEdit(std::int32_t delete_before_cursor,
                        std::string_view commit_text,
                        char raw_key);
    [[nodiscard]] bool shouldStartTokenLiteral(char key) const;
    static bool shouldStartLineLiteral(std::string_view token);
    static std::size_t utf8Characters(std::string_view text);

    static constexpr std::size_t output_capacity = 1024;
    UlEngineContext *context_{};
    std::array<char, output_capacity> output_{};
    std::uint64_t next_sequence_{1};
    std::string raw_token_;
    std::size_t displayed_token_characters_{};
    bool literal_mode_{};
    bool line_literal_mode_{};
    bool quote_literal_mode_{};
};

} // namespace unilume::core
