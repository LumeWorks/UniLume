// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace unilume::fcitx5 {

// Direct replacement is safe only when one logical delete-plus-commit reaches
// the client as one indivisible edit. Split transports are handled by the
// acknowledged uinput path instead.
[[nodiscard]] bool replacementTransportIsAtomic(
    std::string_view frontend_name);

} // namespace unilume::fcitx5
