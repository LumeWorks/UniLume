// SPDX-License-Identifier: GPL-2.0-or-later

#include "status_action_model.h"

#include <fcitx-config/rawconfig.h>

#include <iostream>

namespace {

int failures = 0;

void expect(bool value, const char *message)
{
    if (!value) {
        std::cerr << "FAIL " << message << '\n';
        ++failures;
    }
}

} // namespace

int main()
{
    using namespace unilume::fcitx5;

    StatusSnapshot snapshot;
    snapshot.input_method = ConfigInputMethod::VNI;
    snapshot.spell_check = true;
    expect(statusIsChecked(StatusCommand::select_vni, snapshot),
           "selected input method is checked");
    expect(!statusIsChecked(StatusCommand::select_telex, snapshot),
           "unselected input method is unchecked");
    expect(statusMutation(StatusCommand::select_viqr, snapshot)->value ==
               "VIQR",
           "input method mutation is closed");
    const StatusMutation method_update =
        *statusMutation(StatusCommand::select_viqr, snapshot);
    fcitx::RawConfig raw;
    raw.setValueByPath(method_update.path, method_update.value);
    InputMethodConfig configuration;
    expect(loadInputMethodConfig(configuration, raw) &&
               *configuration.input_method == ConfigInputMethod::VIQR,
           "status mutation applies through the real Fcitx config contract");
    expect(statusMutation(StatusCommand::toggle_spell_check, snapshot)
                   ->value == "False",
           "spell action toggles the current value");

    expect(!statusMutation(StatusCommand::toggle_macros, snapshot),
           "macros cannot enable without a validated file");
    snapshot.macro_file_available = true;
    expect(statusMutation(StatusCommand::toggle_macros, snapshot)->value ==
               "True",
           "macros enable after a validated file is configured");
    snapshot.macros = true;
    snapshot.macro_file_available = false;
    expect(statusMutation(StatusCommand::toggle_macros, snapshot)->value ==
               "False",
           "macros can always be disabled");

    expect(!statusMutation(StatusCommand::toggle_dictionary, snapshot),
           "dictionary cannot enable without a validated file");
    snapshot.dictionary_file_available = true;
    expect(statusMutation(StatusCommand::toggle_dictionary, snapshot)->value ==
               "True",
           "dictionary enables after a validated file is configured");
    expect(!statusMutation(StatusCommand::show_utf8, snapshot),
           "lossless charset status is read-only");
    expect(statusIsChecked(StatusCommand::show_utf8, snapshot),
           "UTF-8 is the active output charset");
    return failures == 0 ? 0 : 1;
}
