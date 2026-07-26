// SPDX-License-Identifier: GPL-2.0-or-later

#include "status_action_model.h"

#include <fcitx-utils/i18n.h>

namespace unilume::fcitx5 {

const char *statusShortText(StatusCommand command)
{
    switch (command) {
    case StatusCommand::select_telex:
        return N_("Input method: Telex");
    case StatusCommand::select_vni:
        return N_("Input method: VNI");
    case StatusCommand::select_viqr:
        return N_("Input method: VIQR");
    case StatusCommand::show_utf8:
        return N_("Output charset: UTF-8");
    case StatusCommand::toggle_spell_check:
        return N_("Spell check");
    case StatusCommand::toggle_macros:
        return N_("Macros");
    case StatusCommand::toggle_dictionary:
        return N_("Personal dictionary");
    }
    return N_("UniLume");
}

const char *statusLongText(StatusCommand command,
                           const StatusSnapshot &snapshot)
{
    switch (command) {
    case StatusCommand::select_telex:
    case StatusCommand::select_vni:
    case StatusCommand::select_viqr:
        return N_(
            "Change the Vietnamese input method at a composition boundary");
    case StatusCommand::show_utf8:
        return N_("UniLume commits lossless UTF-8 output");
    case StatusCommand::toggle_spell_check:
        return N_("Enable or disable UniKey spell checking");
    case StatusCommand::toggle_macros:
        return !snapshot.macros && !snapshot.macro_file_available
                   ? N_("Configure a valid macro file before enabling macros")
                   : N_("Enable or disable word-boundary macros");
    case StatusCommand::toggle_dictionary:
        return !snapshot.dictionary &&
                       !snapshot.dictionary_file_available
                   ? N_("Configure a valid personal dictionary before enabling it")
                   : N_("Enable or disable personal dictionary policy");
    }
    return N_("UniLume status action");
}

bool statusIsCheckable(StatusCommand)
{
    return true;
}

bool statusIsChecked(StatusCommand command,
                     const StatusSnapshot &snapshot)
{
    switch (command) {
    case StatusCommand::select_telex:
        return snapshot.input_method == ConfigInputMethod::Telex;
    case StatusCommand::select_vni:
        return snapshot.input_method == ConfigInputMethod::VNI;
    case StatusCommand::select_viqr:
        return snapshot.input_method == ConfigInputMethod::VIQR;
    case StatusCommand::show_utf8:
        return true;
    case StatusCommand::toggle_spell_check:
        return snapshot.spell_check;
    case StatusCommand::toggle_macros:
        return snapshot.macros;
    case StatusCommand::toggle_dictionary:
        return snapshot.dictionary;
    }
    return false;
}

std::optional<StatusMutation>
statusMutation(StatusCommand command, const StatusSnapshot &snapshot)
{
    switch (command) {
    case StatusCommand::select_telex:
        return StatusMutation{"InputMethod", "Telex"};
    case StatusCommand::select_vni:
        return StatusMutation{"InputMethod", "VNI"};
    case StatusCommand::select_viqr:
        return StatusMutation{"InputMethod", "VIQR"};
    case StatusCommand::show_utf8:
        return std::nullopt;
    case StatusCommand::toggle_spell_check:
        return StatusMutation{
            "SpellCheck", snapshot.spell_check ? "False" : "True"};
    case StatusCommand::toggle_macros:
        if (!snapshot.macros && !snapshot.macro_file_available) {
            return std::nullopt;
        }
        return StatusMutation{
            "MacroEnabled", snapshot.macros ? "False" : "True"};
    case StatusCommand::toggle_dictionary:
        if (!snapshot.dictionary &&
            !snapshot.dictionary_file_available) {
            return std::nullopt;
        }
        return StatusMutation{
            "DictionaryEnabled",
            snapshot.dictionary ? "False" : "True"};
    }
    return std::nullopt;
}

} // namespace unilume::fcitx5
