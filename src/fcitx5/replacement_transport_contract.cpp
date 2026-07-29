// SPDX-License-Identifier: GPL-2.0-or-later

#include "replacement_transport_contract.h"

namespace unilume::fcitx5 {

bool replacementTransportIsAtomic(std::string_view frontend_name)
{
    return frontend_name != "dbus" &&
           frontend_name != "wayland" &&
           frontend_name != "wayland_v2";
}

} // namespace unilume::fcitx5
