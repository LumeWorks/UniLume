// SPDX-License-Identifier: GPL-2.0-or-later

#include "adaptive_router.h"

namespace unilume::platform {

RouteDecision AdaptiveRouter::route(
    const InputCapabilities &capabilities,
    bool quarantined,
    bool disabled)
{
    if (disabled) {
        return {AdaptivePath::passthrough, RouteReason::disabled,
                capabilities.signature};
    }

    if (quarantined) {
        if (capabilities.preedit == PreeditSemantics::client) {
            return {AdaptivePath::client_preedit, RouteReason::quarantined,
                    capabilities.signature};
        }
        if (capabilities.preedit == PreeditSemantics::server) {
            return {AdaptivePath::server_preedit, RouteReason::quarantined,
                    capabilities.signature};
        }
        return {AdaptivePath::passthrough, RouteReason::quarantined,
                capabilities.signature};
    }

    // Atomic replacement requires a real atomic transport (not uinput),
    // a valid surrounding snapshot and a collapsed selection.
    if (capabilities.atomicReplacement() &&
        capabilities.surrounding_snapshot_valid &&
        capabilities.selection_collapsed) {
        return {AdaptivePath::atomic_direct, RouteReason::atomic_available,
                capabilities.signature};
    }

    // Atomic transport exists but the snapshot is stale or selection is
    // expanded.  Fall back to preedit if available.
    if (capabilities.atomicReplacement() && capabilities.anyPreedit()) {
        if (capabilities.preedit == PreeditSemantics::client) {
            return {AdaptivePath::client_preedit,
                    RouteReason::stale_snapshot_preedit_fallback,
                    capabilities.signature};
        }
        return {AdaptivePath::server_preedit,
                RouteReason::stale_snapshot_preedit_fallback,
                capabilities.signature};
    }

    // No atomic transport.  Choose the best preedit path.
    if (capabilities.preedit == PreeditSemantics::client) {
        return {AdaptivePath::client_preedit,
                RouteReason::no_atomic_client_preedit,
                capabilities.signature};
    }
    if (capabilities.preedit == PreeditSemantics::server) {
        return {AdaptivePath::server_preedit,
                RouteReason::no_atomic_server_preedit,
                capabilities.signature};
    }

    return {AdaptivePath::passthrough, RouteReason::no_capability,
            capabilities.signature};
}

bool AdaptiveRouter::shouldHoldRoute(
    const RouteDecision &current,
    const InputCapabilities &new_capabilities)
{
    // Route is held until a boundary; a better capability appearing
    // mid-composition must not promote.
    (void)new_capabilities;
    return current.path != AdaptivePath::passthrough;
}

bool AdaptiveRouter::requiresFence(
    const RouteDecision &current,
    const InputCapabilities &new_capabilities)
{
    // Fence when the current route is no longer safe:
    // - atomic_direct but atomic capability lost
    // - client_preedit but client preedit lost
    // - server_preedit but server preedit lost
    switch (current.path) {
    case AdaptivePath::atomic_direct:
        return !new_capabilities.atomicReplacement();
    case AdaptivePath::client_preedit:
        return new_capabilities.preedit != PreeditSemantics::client;
    case AdaptivePath::server_preedit:
        return new_capabilities.preedit != PreeditSemantics::server;
    case AdaptivePath::passthrough:
        return false;
    }
    return false;
}

} // namespace unilume::platform
