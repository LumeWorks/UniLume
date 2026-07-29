// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "typing_pipeline.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::core {

struct PreeditAction {
    bool handled{};
    std::string_view commit_text;
    std::string_view preedit_text;
};

enum class PreeditCommitPolicy : std::uint8_t {
    word_boundary,
    composition_boundary,
};

class PreeditFallbackController {
public:
    explicit PreeditFallbackController(
        UlInputMethod method = UL_INPUT_METHOD_TELEX,
        PreeditCommitPolicy commit_policy =
            PreeditCommitPolicy::word_boundary);

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
    struct StoredInput {
        KeyKind kind{KeyKind::text};
        std::string text;
        bool shift_pressed{};
        bool caps_lock_on{};
        bool has_control_modifier{};

        [[nodiscard]] KeyInput view() const;
    };

    struct BoundaryCheckpoint {
        std::size_t token_start{};
        std::vector<StoredInput> token_inputs;
    };

    bool applyEdit(std::int32_t delete_before_cursor,
                   std::string_view commit_text);
    static bool applyEdit(std::string &text,
                          std::int32_t delete_before_cursor,
                          std::string_view commit_text);
    bool restoreBeforeBoundary();
    void record(const KeyInput &input);
    void clearEditingState();
    void commitPending(std::string_view suffix);
    static std::size_t previousCharacter(std::string_view text,
                                         std::size_t position);

    TypingPipeline engine_;
    std::string preedit_;
    std::string commit_;
    PreeditCommitPolicy commit_policy_;
    std::vector<StoredInput> token_inputs_;
    std::vector<BoundaryCheckpoint> boundaries_;
    std::size_t token_start_{};
    bool detached_preedit_{};
};

} // namespace unilume::core
