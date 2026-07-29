// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace unilume::fcitx5 {

// Direct replacement is safe only when one logical delete-plus-commit reaches
// the client as one indivisible edit. Fcitx's asynchronous D-Bus and Wayland
// input-method frontends do not provide that contract, so they must use
// preedit until Fcitx exposes an atomic replacement primitive to addons.
[[nodiscard]] bool replacementTransportIsAtomic(
    std::string_view frontend_name);

} // namespace unilume::fcitx5
