// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "typing_pipeline.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace unilume::core {

struct PreeditAction {
    bool handled{};
    std::string_view commit_text;
    std::string_view preedit_text;
};

class PreeditFallbackController {
public:
    explicit PreeditFallbackController(
        UlInputMethod method = UL_INPUT_METHOD_TELEX);

    PreeditAction submit(const KeyInput &input);
    void reset();
    void lineBreak();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);
    void setTypingOptions(const TypingConvenienceOptions &options);
    void setMacros(const macro::Snapshot &snapshot);
    void setKeymap(const keymap::Snapshot &snapshot);
    void setDictionary(const dictionary::Snapshot &snapshot);

    [[nodiscard]] std::string_view preedit() const;

private:
    bool applyEdit(std::int32_t delete_before_cursor,
                   std::string_view commit_text);
    void commitPending(std::string_view suffix);
    static std::size_t previousCharacter(std::string_view text,
                                         std::size_t position);

    TypingPipeline engine_;
    std::string preedit_;
    std::string commit_;
};

} // namespace unilume::core
