// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace unilume::fcitx5 {

// Developer-only route override for the experimental uinput direct path.
// Only the exact token `direct_experimental` is accepted; any other
// value (typo, whitespace, wrong case) collapses to `none` so the
// uinput path can never be opened by accident.
enum class DeveloperRouteOverride : std::uint8_t {
    none,
    direct_experimental,
};

// Strict parser for DeveloperRouteOverride.  Returns `none` for any
// value that is not exactly "direct_experimental" (case-sensitive, no
// leading/trailing whitespace).  This keeps a malformed config from
// accidentally unlocking the experimental uinput path.
[[nodiscard]] DeveloperRouteOverride parseDeveloperRouteOverride(
    std::string_view value);

[[nodiscard]] std::string_view developerRouteOverrideName(
    DeveloperRouteOverride override);

} // namespace unilume::fcitx5
