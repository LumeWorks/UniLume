// SPDX-License-Identifier: GPL-2.0-or-later

#include "developer_route_override.h"

namespace unilume::fcitx5 {

DeveloperRouteOverride parseDeveloperRouteOverride(std::string_view value)
{
    if (value == "direct_experimental") {
        return DeveloperRouteOverride::direct_experimental;
    }
    return DeveloperRouteOverride::none;
}

std::string_view developerRouteOverrideName(DeveloperRouteOverride override)
{
    switch (override) {
    case DeveloperRouteOverride::none:
        return "none";
    case DeveloperRouteOverride::direct_experimental:
        return "direct_experimental";
    }
    return "none";
}

} // namespace unilume::fcitx5
