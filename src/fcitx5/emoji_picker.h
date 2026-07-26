// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "emoji_model.h"

#include <fcitx-utils/inputbuffer.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>

#include <filesystem>
#include <memory>

namespace unilume::fcitx5 {

class EmojiPickerState final : public fcitx::InputContextProperty {
public:
    EmojiPickerState();

    bool active{};
    fcitx::InputBuffer query;
};

class EmojiPicker final {
public:
    EmojiPicker(fcitx::Instance &instance,
                std::filesystem::path history_path);

    [[nodiscard]] bool available();
    [[nodiscard]] bool trigger(fcitx::InputContext *input_context);
    [[nodiscard]] bool handle(fcitx::KeyEvent &event);
    [[nodiscard]] bool active(fcitx::InputContext *input_context) const;
    void reset(fcitx::InputContext *input_context);
    [[nodiscard]] bool clearHistory();
    void commit(fcitx::InputContext *input_context,
                const std::string &glyph);

private:
    [[nodiscard]] bool ensureData();
    void updateUi(fcitx::InputContext *input_context);
    [[nodiscard]] EmojiPickerState *stateFor(
        fcitx::InputContext *input_context) const;

    fcitx::Instance &instance_;
    fcitx::FactoryFor<EmojiPickerState> state_factory_;
    emoji::HistoryStore history_;
    std::unique_ptr<emoji::SearchIndex> index_;
    fcitx::AddonInstance *module_{};
    bool load_attempted_{};
    bool history_loaded_{};
};

} // namespace unilume::fcitx5
