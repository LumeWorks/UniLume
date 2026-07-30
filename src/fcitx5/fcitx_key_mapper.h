// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "key_input.h"

#include <array>
#include <cstddef>
#include <fcitx/event.h>
#include <fcitx-utils/key.h>

namespace unilume::fcitx5 {

enum class MappingStatus {
    ignored,
    submit,
    reset,
    line_break,
    shortcut_fence,
    plain_backspace,
};

struct MappedKey {
    MappingStatus status{MappingStatus::ignored};
    core::KeyKind kind{core::KeyKind::text};
    std::array<char, 8> text{};
    std::size_t text_size{};
    bool shift_pressed{};
    bool caps_lock_on{};
    bool has_control_modifier{};

    [[nodiscard]] core::KeyInput input() const;
};

MappedKey mapKeyEvent(const fcitx::KeyEvent &event);
MappedKey mapKey(const fcitx::Key &key,
                 const fcitx::Key &raw_key,
                 bool release = false);

} // namespace unilume::fcitx5
