// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace unilume::fcitx5 {

enum class DirectStrategy : std::uint8_t {
    fast,
    guarded,
};

} // namespace unilume::fcitx5
