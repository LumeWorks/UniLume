// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace unilume::fcitx5 {

// Direct replacement is safe only when one logical delete-plus-commit reaches
// the client as one indivisible edit. Fcitx's Wayland input-method frontends
// currently flush delete and commit separately, so they must use preedit until
// Fcitx exposes an atomic replacement primitive to input-method addons.
[[nodiscard]] bool replacementTransportIsAtomic(
    std::string_view frontend_name);

} // namespace unilume::fcitx5
