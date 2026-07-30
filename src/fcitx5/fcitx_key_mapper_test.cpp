// SPDX-License-Identifier: GPL-2.0-or-later

#include "fcitx_key_mapper.h"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        ++failures;
    }
}

fcitx::Key modified(FcitxKeySym sym, fcitx::KeyState state)
{
    return fcitx::Key(sym, fcitx::KeyStates(state));
}

} // namespace

int main()
{
    using namespace unilume::fcitx5;

    for (const fcitx::KeyState modifier : {
             fcitx::KeyState::Ctrl,
             fcitx::KeyState::Alt,
             fcitx::KeyState::Super,
             fcitx::KeyState::Super2,
             fcitx::KeyState::Hyper,
             fcitx::KeyState::Hyper2,
             fcitx::KeyState::Meta,
             fcitx::KeyState::Mod5,
         }) {
        const fcitx::Key key = modified(FcitxKey_a, modifier);
        const MappedKey mapped = mapKey(key, key);
        expect(mapped.status == MappingStatus::shortcut_fence,
               "modified printable key was not passed through");
    }

    const fcitx::Key shift_tab =
        modified(FcitxKey_ISO_Left_Tab, fcitx::KeyState::Shift);
    expect(mapKey(shift_tab, shift_tab).status == MappingStatus::shortcut_fence,
           "Shift+Tab was not passed through");

    const fcitx::Key shift_enter =
        modified(FcitxKey_Return, fcitx::KeyState::Shift);
    expect(mapKey(shift_enter, shift_enter).status == MappingStatus::shortcut_fence,
           "Shift+Enter was not passed through");

    const fcitx::Key ctrl_backspace =
        modified(FcitxKey_BackSpace, fcitx::KeyState::Ctrl);
    expect(mapKey(ctrl_backspace, ctrl_backspace).status == MappingStatus::shortcut_fence,
           "Ctrl+Backspace was not passed through");

    const fcitx::Key shift_backspace =
        modified(FcitxKey_BackSpace, fcitx::KeyState::Shift);
    expect(mapKey(shift_backspace, shift_backspace).status == MappingStatus::shortcut_fence,
           "Shift+Backspace was not passed through");

    for (const FcitxKeySym symbol : {
             FcitxKey_Tab, FcitxKey_Left, FcitxKey_Delete,
             FcitxKey_Escape,
         }) {
        const fcitx::Key key(symbol);
        expect(mapKey(key, key).status == MappingStatus::shortcut_fence,
               "non-text key was not passed through");
    }

    const fcitx::Key enter(FcitxKey_Return);
    expect(mapKey(enter, enter).status == MappingStatus::line_break,
           "Enter did not preserve its line-break boundary");

    const fcitx::Key backspace(FcitxKey_BackSpace);
    const MappedKey mapped_backspace = mapKey(backspace, backspace);
    expect(mapped_backspace.status == MappingStatus::plain_backspace &&
               mapped_backspace.kind == unilume::core::KeyKind::backspace,
           "plain Backspace did not reach the engine");

    const fcitx::Key shifted_a =
        modified(FcitxKey_A, fcitx::KeyState::Shift);
    const MappedKey mapped_shift = mapKey(shifted_a, shifted_a);
    expect(mapped_shift.status == MappingStatus::submit &&
               mapped_shift.input().text == "A" &&
               mapped_shift.input().shift_pressed,
           "Shift+printable did not reach the engine");

    const fcitx::Key release(FcitxKey_a);
    expect(mapKey(release, release, true).status == MappingStatus::ignored,
           "ordinary key release was not ignored");

    return failures == 0 ? 0 : 1;
}
