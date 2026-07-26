// SPDX-License-Identifier: GPL-2.0-or-later

#include "fcitx_key_mapper.h"

#include <algorithm>
#include <fcitx-utils/key.h>

namespace unilume::fcitx5 {

core::KeyInput MappedKey::input() const
{
    return {
        kind,
        std::string_view{text.data(), text_size},
        shift_pressed,
        caps_lock_on,
        has_control_modifier,
    };
}

MappedKey mapKeyEvent(const fcitx::KeyEvent &event)
{
    MappedKey mapped;
    if (event.isRelease()) {
        return mapped;
    }

    const fcitx::Key key = event.key();
    const fcitx::KeyStates states = event.rawKey().states();
    mapped.shift_pressed = states.test(fcitx::KeyState::Shift);
    mapped.caps_lock_on = states.test(fcitx::KeyState::CapsLock);
    mapped.has_control_modifier =
        states.testAny(fcitx::KeyState::Ctrl_Alt) ||
        states.test(fcitx::KeyState::Super) ||
        states.test(fcitx::KeyState::Super2) ||
        states.test(fcitx::KeyState::Meta);

    if (key.check(FcitxKey_BackSpace)) {
        mapped.status = MappingStatus::submit;
        mapped.kind = core::KeyKind::backspace;
        return mapped;
    }
    if (key.check(FcitxKey_Return)) {
        mapped.status = MappingStatus::line_break;
        mapped.kind = core::KeyKind::navigation;
        return mapped;
    }
    if (key.isCursorMove() || key.check(FcitxKey_Delete) ||
        key.check(FcitxKey_Tab)) {
        mapped.status = MappingStatus::reset;
        mapped.kind = core::KeyKind::navigation;
        return mapped;
    }

    const std::string text = fcitx::Key::keySymToUTF8(key.sym());
    if (text.empty() || text.size() > mapped.text.size()) {
        return mapped;
    }
    std::copy(text.begin(), text.end(), mapped.text.begin());
    mapped.text_size = text.size();
    mapped.status = MappingStatus::submit;
    return mapped;
}

} // namespace unilume::fcitx5
