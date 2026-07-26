// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "input_method_config.h"

#include <optional>
#include <string>

namespace unilume::fcitx5 {

enum class StatusCommand {
    select_telex,
    select_vni,
    select_viqr,
    show_utf8,
    toggle_spell_check,
    toggle_macros,
    toggle_dictionary,
};

struct StatusSnapshot {
    ConfigInputMethod input_method{ConfigInputMethod::Telex};
    bool spell_check{};
    bool macros{};
    bool dictionary{};
    bool macro_file_available{};
    bool dictionary_file_available{};
};

struct StatusMutation {
    std::string path;
    std::string value;
};

[[nodiscard]] const char *statusShortText(StatusCommand command);
[[nodiscard]] const char *statusLongText(StatusCommand command,
                                         const StatusSnapshot &snapshot);
[[nodiscard]] bool statusIsCheckable(StatusCommand command);
[[nodiscard]] bool statusIsChecked(StatusCommand command,
                                   const StatusSnapshot &snapshot);
[[nodiscard]] std::optional<StatusMutation>
statusMutation(StatusCommand command, const StatusSnapshot &snapshot);

} // namespace unilume::fcitx5
