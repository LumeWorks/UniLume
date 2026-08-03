// SPDX-License-Identifier: GPL-2.0-or-later

#include "route_health_registry.h"

namespace unilume::platform {

void RouteHealthRegistry::quarantine(
    const RouteHealthKey &key,
    std::uint64_t generation)
{
    entries_[key] = {true, generation};
}

bool RouteHealthRegistry::isQuarantined(const RouteHealthKey &key) const
{
    const auto it = entries_.find(key);
    return it != entries_.end() && it->second.quarantined;
}

RouteHealth RouteHealthRegistry::health(const RouteHealthKey &key) const
{
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return {};
    }
    return it->second;
}

void RouteHealthRegistry::clear(const RouteHealthKey &key)
{
    entries_.erase(key);
}

void RouteHealthRegistry::clearAll()
{
    entries_.clear();
}

std::size_t RouteHealthRegistry::size() const
{
    return entries_.size();
}

} // namespace unilume::platform
