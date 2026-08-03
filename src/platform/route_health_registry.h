// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "adaptive_router.h"

#include <unordered_map>

namespace unilume::platform {

// Session-local health registry for adaptive routing.  Not persisted
// across reboots.  Program identity is NOT part of the key.
class RouteHealthRegistry {
public:
    // Mark a route health key as quarantined at the given generation.
    void quarantine(
        const RouteHealthKey &key,
        std::uint64_t generation);

    // Whether the key is currently quarantined.
    [[nodiscard]] bool isQuarantined(const RouteHealthKey &key) const;

    // Get the health state for a key.
    [[nodiscard]] RouteHealth health(const RouteHealthKey &key) const;

    // Clear quarantine for a specific key (e.g. capability signature
    // changed or developer reset).
    void clear(const RouteHealthKey &key);

    // Clear all quarantine entries (developer reset).
    void clearAll();

    // Number of quarantined entries (for diagnostics/tests).
    [[nodiscard]] std::size_t size() const;

private:
    struct KeyHash {
        std::size_t operator()(const RouteHealthKey &key) const noexcept
        {
            std::size_t h = key.context_id;
            h ^= std::hash<std::string>{}(key.frontend) + 0x9e3779b9 +
                 (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(key.display) + 0x9e3779b9 +
                 (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.semantics) + 0x9e3779b9 +
                 (h << 6) + (h >> 2);
            h ^= key.capability_signature + 0x9e3779b9 +
                 (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<RouteHealthKey, RouteHealth, KeyHash> entries_;
};

} // namespace unilume::platform
