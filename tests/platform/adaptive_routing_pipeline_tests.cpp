// SPDX-License-Identifier: GPL-2.0-or-later

// Composed-pipeline tests for the adaptive routing integration.  These
// exercise the real primitives (buildCapabilities, AdaptiveRouter and
// RouteHealthRegistry) the way InputContextState::synchronizeAdaptive
// composes them: build a capability snapshot from a runtime observation,
// consult the health registry, route, and on an uncertain terminal
// quarantine the route so the next composition falls back to preedit.
//
// InputContextState itself cannot be constructed without a live
// fcitx::InputContext, so these tests cover the testable decision core.

#include "adaptive_router.h"
#include "capability_builder.h"
#include "route_health_registry.h"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

using namespace unilume::platform;

// A runtime observation as InputContextState would read it from the
// replacement backend and the input-context capability flags.
struct RuntimeState {
    bool atomic_transport{};
    bool acknowledged_uinput{};
    bool surrounding_available{};
    bool cursor_valid{};
    bool utf8_valid{};
    bool within_resource_limit{};
    bool poisoned{};
    bool client_preedit_flag{};
    bool server_preedit_available{true};
    bool verified_direct_enabled{true};
    std::uint64_t generation{1};
};

// The health identity is fixed for one input context in these scenarios.
struct Identity {
    std::uint64_t context_id{42};
    std::string frontend{"wayland"};
    std::string display{":0"};
};

// One routing pass mirroring synchronizeAdaptive's decision core.
// Returns the chosen path and updates `route` and `registry`.
struct RouteState {
    RouteDecision decision{};
    RouteHealthKey key{};
    bool active{};
};

RouteState routeOnce(const RuntimeState &s, const Identity &id,
                     RouteHealthRegistry &registry, RouteState &route)
{
    ReplacementSemantics replacement = ReplacementSemantics::none;
    if (s.verified_direct_enabled && !s.poisoned) {
        if (s.atomic_transport) {
            replacement = ReplacementSemantics::client_atomic_event;
        } else if (s.acknowledged_uinput) {
            replacement = ReplacementSemantics::split_unverified;
        }
    }
    const PreeditSemantics preedit = s.client_preedit_flag
        ? PreeditSemantics::client
        : (s.server_preedit_available ? PreeditSemantics::server
                                      : PreeditSemantics::none);
    const bool snapshot_valid =
        s.cursor_valid && s.utf8_valid && s.within_resource_limit;

    const InputCapabilities caps = buildCapabilities(
        replacement, preedit, s.surrounding_available, snapshot_valid,
        true, s.generation);

    const RouteHealthKey key{id.context_id, id.frontend, id.display,
                             caps.replacement, caps.signature};
    const bool quarantined = registry.isQuarantined(key);

    if (AdaptiveRouter::shouldHoldRoute(route.decision, caps)) {
        if (AdaptiveRouter::requiresFence(route.decision, caps)) {
            // fence: composition boundary resets the route.
            route = {};
        } else {
            return route;  // hold
        }
    }

    const RouteDecision decision =
        AdaptiveRouter::route(caps, quarantined, false);
    route.decision = decision;
    route.key = key;
    route.active = (decision.path != AdaptivePath::passthrough);
    return route;
}

// Simulate an uncertain terminal: quarantine the atomic route and end
// the composition (clear the active route), mirroring
// handleUncertainCompletion + the fence-free re-route on the next key.
void uncertainTerminal(RouteState &route, RouteHealthRegistry &registry,
                       std::uint64_t generation)
{
    if (route.decision.path == AdaptivePath::atomic_direct) {
        registry.quarantine(route.key, generation);
    }
    route = {};
}

RuntimeState atomicValidState()
{
    return {true, false, true, true, true, true, false, true, true, 1};
}

RuntimeState atomicStaleState()
{
    return {true, false, true, false, true, true, false, true, true, 1};
}

RuntimeState uinputWithClientPreeditState()
{
    return {false, true, false, false, false, false, false, true, true, 1};
}

RuntimeState noAtomicClientPreeditState()
{
    return {false, false, false, false, false, false, false, true, true, 1};
}

RuntimeState noCapabilityState()
{
    return {false, false, false, false, false, false, false, false,
            false, true, 1};
}

void testAtomicValidRoutesDirect()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    const auto r = routeOnce(atomicValidState(), id, registry, route);
    expect(r.decision.path == AdaptivePath::atomic_direct,
           "atomic + valid snapshot -> atomic_direct");
    expect(r.active, "atomic route is active");
}

void testAtomicStaleFallsBackToPreedit()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    const auto r = routeOnce(atomicStaleState(), id, registry, route);
    expect(r.decision.path == AdaptivePath::client_preedit,
           "atomic + stale snapshot -> client_preedit");
}

void testUinputNeverDirectInAutomatic()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    const auto r =
        routeOnce(uinputWithClientPreeditState(), id, registry, route);
    expect(r.decision.path != AdaptivePath::atomic_direct,
           "uinput never selected as atomic_direct");
    expect(r.decision.path == AdaptivePath::client_preedit,
           "uinput + client preedit -> client_preedit");
}

void testNoAtomicClientPreedit()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    const auto r =
        routeOnce(noAtomicClientPreeditState(), id, registry, route);
    expect(r.decision.path == AdaptivePath::client_preedit,
           "no atomic + client preedit -> client_preedit");
}

void testNoCapabilityPassthrough()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    const auto r = routeOnce(noCapabilityState(), id, registry, route);
    expect(r.decision.path == AdaptivePath::passthrough,
           "no capability -> passthrough");
    expect(!r.active, "passthrough is not active");
}

void testPoisonSuppressesAtomic()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    RuntimeState s = atomicValidState();
    s.poisoned = true;
    const auto r = routeOnce(s, id, registry, route);
    expect(r.decision.path == AdaptivePath::client_preedit,
           "poisoned atomic -> client_preedit (poison suppresses atomic)");
}

void testVerifiedDirectDisabledSuppressesAtomic()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    RuntimeState s = atomicValidState();
    s.verified_direct_enabled = false;
    const auto r = routeOnce(s, id, registry, route);
    expect(r.decision.path == AdaptivePath::client_preedit,
           "verified-direct disabled -> client_preedit");
}

void testStickinessHoldsAcrossKeys()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    // First key: atomic direct.
    routeOnce(atomicValidState(), id, registry, route);
    expect(route.decision.path == AdaptivePath::atomic_direct,
           "first key atomic_direct");
    // Second key: same capability -> held (no re-route).
    const auto held = routeOnce(atomicValidState(), id, registry, route);
    expect(held.decision.path == AdaptivePath::atomic_direct,
           "second key holds atomic_direct");
    // A strictly better signature must NOT promote mid-composition.
    RuntimeState better = atomicValidState();
    const InputCapabilities better_caps = buildCapabilities(
        ReplacementSemantics::client_atomic_event,
        PreeditSemantics::client, true, true, true, 2);
    expect(!AdaptiveRouter::requiresFence(route.decision, better_caps),
           "better capability does not fence");
}

void testFenceOnAtomicLoss()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    routeOnce(atomicValidState(), id, registry, route);
    expect(route.decision.path == AdaptivePath::atomic_direct,
           "start atomic_direct");
    // Atomic capability lost mid-composition.
    const auto fenced =
        routeOnce(noAtomicClientPreeditState(), id, registry, route);
    expect(fenced.decision.path == AdaptivePath::client_preedit,
           "atomic lost -> fence -> client_preedit");
}

void testUncertainQuarantinesToPreedit()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    // Composition uses atomic direct.
    routeOnce(atomicValidState(), id, registry, route);
    expect(route.decision.path == AdaptivePath::atomic_direct,
           "atomic_direct before uncertain");
    // Uncertain terminal quarantines and ends the composition.
    uncertainTerminal(route, registry, 1);
    // Next composition, same capability: quarantine forces preedit.
    const auto after =
        routeOnce(atomicValidState(), id, registry, route);
    expect(after.decision.path == AdaptivePath::client_preedit,
           "after uncertain, same signature -> client_preedit (quarantined)");
    expect(registry.isQuarantined(route.key),
           "registry still quarantined for the atomic signature");
}

void testQuarantineSurvivesFocusReset()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    routeOnce(atomicValidState(), id, registry, route);
    uncertainTerminal(route, registry, 1);
    // A focus reset clears the active route but NOT the registry.
    route = {};
    // Next composition: still quarantined (registry persisted).
    const auto after =
        routeOnce(atomicValidState(), id, registry, route);
    expect(after.decision.path == AdaptivePath::client_preedit,
           "quarantine survives focus reset -> client_preedit");
}

void testQuarantineSurvivesSnapshotOnlyChange()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    // Atomic direct with surrounding-text capability + valid snapshot.
    routeOnce(atomicValidState(), id, registry, route);
    const RouteHealthKey quarantined_key = route.key;
    uncertainTerminal(route, registry, 1);
    expect(registry.isQuarantined(quarantined_key),
           "quarantined after uncertain");
    // Simulate "text changed around the cursor": a new snapshot
    // generation arrives but the surrounding-text CAPABILITY is unchanged.
    // The signature must be identical, so the quarantine key still matches
    // and the next composition falls back to preedit instead of retrying
    // atomic.  This is the regression guard for Issue #127: quarantine must
    // not drop when only the snapshot content changes.
    RuntimeState text_changed = atomicValidState();
    text_changed.generation = 99;
    expect(text_changed.surrounding_available,
           "capability unchanged after text edit");
    const auto after =
        routeOnce(text_changed, id, registry, route);
    expect(route.key == quarantined_key,
           "snapshot-only change keeps same signature/key");
    expect(registry.isQuarantined(route.key),
           "quarantine survives snapshot-only change");
    expect(after.decision.path == AdaptivePath::client_preedit,
           "quarantine forces preedit despite valid atomic snapshot");
}

void testQuarantineClearsOnSignatureChange()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    routeOnce(atomicValidState(), id, registry, route);
    uncertainTerminal(route, registry, 1);
    expect(registry.size() == 1, "one quarantined key");
    // The capability signature changes (e.g. surrounding text lost): the
    // new key differs, so the registry does not quarantine it and atomic
    // direct is available again for the new signature.
    RuntimeState changed = atomicValidState();
    changed.surrounding_available = false;
    const auto after = routeOnce(changed, id, registry, route);
    // Surrounding-text capability is part of the signature, so the new
    // key is not the quarantined one.
    expect(!registry.isQuarantined(route.key),
           "new signature not quarantined");
    // atomic transport is still present and (for the no-surrounding
    // atomic case) the snapshot is treated as valid, so it routes direct.
    expect(after.decision.path == AdaptivePath::atomic_direct,
           "new signature -> atomic_direct again");
    expect(registry.size() == 1, "old quarantine entry still present");
}

void testQuarantineClearsOnTransportChange()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    routeOnce(atomicValidState(), id, registry, route);
    uncertainTerminal(route, registry, 1);
    // Transport changes from client_atomic_event to protocol_transaction
    // (a future Wayland atomic path): the signature changes, so the new
    // key is not quarantined.
    RuntimeState proto = atomicValidState();
    // Simulate a protocol-transaction transport: build capabilities
    // directly with the new semantics.
    const InputCapabilities caps = buildCapabilities(
        ReplacementSemantics::protocol_transaction,
        PreeditSemantics::client, true, true, true, 2);
    const RouteHealthKey key{id.context_id, id.frontend, id.display,
                             caps.replacement, caps.signature};
    expect(!registry.isQuarantined(key),
           "protocol-transaction signature not quarantined");
    const auto d = AdaptiveRouter::route(caps, false, false);
    expect(d.path == AdaptivePath::atomic_direct,
           "protocol-transaction -> atomic_direct");
}

void testDeveloperClearAll()
{
    RouteHealthRegistry registry;
    RouteState route;
    const Identity id;
    routeOnce(atomicValidState(), id, registry, route);
    const RouteHealthKey quarantined_key = route.key;
    uncertainTerminal(route, registry, 1);
    expect(registry.isQuarantined(quarantined_key),
           "quarantined before clearAll");
    registry.clearAll();
    const auto after =
        routeOnce(atomicValidState(), id, registry, route);
    expect(after.decision.path == AdaptivePath::atomic_direct,
           "after clearAll, atomic_direct available again");
}

} // namespace

int main()
{
    testAtomicValidRoutesDirect();
    testAtomicStaleFallsBackToPreedit();
    testUinputNeverDirectInAutomatic();
    testNoAtomicClientPreedit();
    testNoCapabilityPassthrough();
    testPoisonSuppressesAtomic();
    testVerifiedDirectDisabledSuppressesAtomic();
    testStickinessHoldsAcrossKeys();
    testFenceOnAtomicLoss();
    testUncertainQuarantinesToPreedit();
    testQuarantineSurvivesFocusReset();
    testQuarantineSurvivesSnapshotOnlyChange();
    testQuarantineClearsOnSignatureChange();
    testQuarantineClearsOnTransportChange();
    testDeveloperClearAll();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All adaptive routing pipeline tests passed\n";
    return 0;
}
