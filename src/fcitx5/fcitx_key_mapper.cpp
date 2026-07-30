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
    return mapKey(event.key(), event.rawKey(), event.isRelease());
}

MappedKey mapKey(const fcitx::Key &key,
                 const fcitx::Key &raw_key,
                 bool release)
{
    MappedKey mapped;
    if (release) {
        return mapped;
    }

    const fcitx::KeyStates states = raw_key.states();
    mapped.shift_pressed = states.test(fcitx::KeyState::Shift);
    mapped.caps_lock_on = states.test(fcitx::KeyState::CapsLock);
    mapped.has_control_modifier =
        states.test(fcitx::KeyState::Ctrl) ||
        states.test(fcitx::KeyState::Alt) ||
        states.testAny(fcitx::KeyState::Ctrl_Alt) ||
        states.test(fcitx::KeyState::Super) ||
        states.test(fcitx::KeyState::Super2) ||
        states.test(fcitx::KeyState::Hyper) ||
        states.test(fcitx::KeyState::Hyper2) ||
        states.test(fcitx::KeyState::Meta) ||
        states.test(fcitx::KeyState::Mod5);

    if (mapped.has_control_modifier) {
        mapped.status = MappingStatus::shortcut_fence;
        mapped.kind = core::KeyKind::reset;
        return mapped;
    }

    const bool is_backspace =
        key.check(FcitxKey_BackSpace) ||
        raw_key.sym() == FcitxKey_BackSpace ||
        raw_key.sym() == 8;

    if (is_backspace) {
        if (mapped.shift_pressed || mapped.has_control_modifier) {
            mapped.status = MappingStatus::shortcut_fence;
            mapped.kind = core::KeyKind::reset;
            return mapped;
        }
        mapped.status = MappingStatus::plain_backspace;
        mapped.kind = core::KeyKind::backspace;
        return mapped;
    }

    const bool is_enter =
        key.sym() == FcitxKey_Return ||
        key.sym() == FcitxKey_KP_Enter ||
        raw_key.sym() == FcitxKey_Return ||
        raw_key.sym() == FcitxKey_KP_Enter;

    if (is_enter) {
        if (mapped.shift_pressed || mapped.has_control_modifier) {
            mapped.status = MappingStatus::shortcut_fence;
            mapped.kind = core::KeyKind::reset;
            return mapped;
        }
        mapped.status = MappingStatus::line_break;
        mapped.kind = core::KeyKind::navigation;
        return mapped;
    }

    if (key.isCursorMove() ||
        key.sym() == FcitxKey_Delete ||
        raw_key.sym() == FcitxKey_Delete ||
        key.sym() == FcitxKey_Tab ||
        raw_key.sym() == FcitxKey_Tab ||
        key.sym() == FcitxKey_KP_Tab ||
        raw_key.sym() == FcitxKey_KP_Tab ||
        raw_key.sym() == FcitxKey_ISO_Left_Tab ||
        key.sym() == FcitxKey_Escape ||
        raw_key.sym() == FcitxKey_Escape) {
        mapped.status = MappingStatus::shortcut_fence;
        mapped.kind = core::KeyKind::navigation;
        return mapped;
    }

    const std::string text = fcitx::Key::keySymToUTF8(key.sym());
    if (text.empty() || text.size() > mapped.text.size()) {
        if (!key.isModifier()) {
            mapped.status = MappingStatus::shortcut_fence;
            mapped.kind = core::KeyKind::reset;
        }
        return mapped;
    }
    std::copy(text.begin(), text.end(), mapped.text.begin());
    mapped.text_size = text.size();
    mapped.status = MappingStatus::submit;
    return mapped;
}

} // namespace unilume::fcitx5
