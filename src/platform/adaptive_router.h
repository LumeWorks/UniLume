// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "capability_model.h"

#include <cstdint>
#include <string>

namespace unilume::platform {

// The path selected by the adaptive router for the current composition.
enum class AdaptivePath {
    atomic_direct,
    client_preedit,
    server_preedit,
    passthrough,
};

// Why a path was selected.  Used for diagnostics and tests.
enum class RouteReason {
    atomic_available,
    stale_snapshot_preedit_fallback,
    no_atomic_client_preedit,
    no_atomic_server_preedit,
    no_capability,
    quarantined,
    disabled,
};

// The result of a routing decision.
struct RouteDecision {
    AdaptivePath path{AdaptivePath::passthrough};
    RouteReason reason{RouteReason::no_capability};
    std::uint64_t capability_signature{};

    bool operator==(const RouteDecision &) const = default;
};

// State of the composition owned by a controller.
enum class CompositionState {
    inactive,
    active,
    completed,
    reset_required,
};

// Health key scoped to an input context, frontend and capability
// signature.  Program identity is NOT part of the key; it is used only
// for diagnostics and policy.
struct RouteHealthKey {
    std::uint64_t context_id{};
    std::string frontend;
    std::string display;
    ReplacementSemantics semantics{ReplacementSemantics::none};
    std::uint64_t capability_signature{};

    bool operator==(const RouteHealthKey &) const = default;
};

// Health state for one route health key.
struct RouteHealth {
    bool quarantined{};
    std::uint64_t quarantine_generation{};

    bool operator==(const RouteHealth &) const = default;
};

// Pure-logic adaptive router.  No Fcitx dependencies; unit-testable.
class AdaptiveRouter {
public:
    // Decide the route for a new composition.
    //
    // Precedence:
    //   1. disabled → passthrough
    //   2. quarantined → best preedit or passthrough
    //   3. atomic + valid snapshot → atomic_direct
    //   4. atomic + stale snapshot + preedit → preedit
    //   5. no atomic + client preedit → client_preedit
    //   6. no atomic + server preedit → server_preedit
    //   7. no capability → passthrough
    //
    // `split_unverified` (uinput) is never selected as atomic_direct by
    // the adaptive router regardless of surrounding snapshot state.
    static RouteDecision route(
        const InputCapabilities &capabilities,
        bool quarantined,
        bool disabled);

    // Whether the current route should remain locked for the rest of
    // the composition.
    static bool shouldHoldRoute(
        const RouteDecision &current,
        const InputCapabilities &new_capabilities);

    // Whether a capability change requires a fence/reset of the
    // current composition.
    static bool requiresFence(
        const RouteDecision &current,
        const InputCapabilities &new_capabilities);
};

} // namespace unilume::platform
