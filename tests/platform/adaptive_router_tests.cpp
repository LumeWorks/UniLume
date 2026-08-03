// SPDX-License-Identifier: GPL-2.0-or-later

#include "adaptive_router.h"
#include "route_health_registry.h"

#include <iostream>
#include <stdexcept>

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

InputCapabilities atomicValid()
{
    return {ReplacementSemantics::protocol_transaction,
            PreeditSemantics::client,
            true, true, true, 1, 100};
}

InputCapabilities atomicStaleSnapshot()
{
    return {ReplacementSemantics::protocol_transaction,
            PreeditSemantics::client,
            true, false, true, 1, 101};
}

InputCapabilities uinputWithClientPreedit()
{
    return {ReplacementSemantics::split_unverified,
            PreeditSemantics::client,
            false, false, true, 1, 200};
}

InputCapabilities uinputWithServerPreedit()
{
    return {ReplacementSemantics::split_unverified,
            PreeditSemantics::server,
            false, false, true, 1, 201};
}

InputCapabilities uinputNoPreedit()
{
    return {ReplacementSemantics::split_unverified,
            PreeditSemantics::none,
            false, false, true, 1, 202};
}

InputCapabilities noCapability()
{
    return {ReplacementSemantics::none,
            PreeditSemantics::none,
            false, false, true, 1, 300};
}

InputCapabilities noAtomicClientPreedit()
{
    return {ReplacementSemantics::none,
            PreeditSemantics::client,
            false, false, true, 1, 301};
}

InputCapabilities noAtomicServerPreedit()
{
    return {ReplacementSemantics::none,
            PreeditSemantics::server,
            false, false, true, 1, 302};
}

void testRouting()
{
    // atomic + valid snapshot → atomic_direct
    {
        const auto d = AdaptiveRouter::route(atomicValid(), false, false);
        expect(d.path == AdaptivePath::atomic_direct,
               "atomic + valid → atomic_direct");
        expect(d.reason == RouteReason::atomic_available,
               "atomic + valid reason");
        expect(d.capability_signature == 100,
               "atomic + valid signature");
    }

    // atomic + stale snapshot + client preedit → client_preedit
    {
        const auto d = AdaptiveRouter::route(atomicStaleSnapshot(), false, false);
        expect(d.path == AdaptivePath::client_preedit,
               "atomic + stale → client_preedit");
        expect(d.reason == RouteReason::stale_snapshot_preedit_fallback,
               "atomic + stale reason");
    }

    // atomic + stale snapshot + server preedit → server_preedit
    {
        InputCapabilities caps = atomicStaleSnapshot();
        caps.preedit = PreeditSemantics::server;
        const auto d = AdaptiveRouter::route(caps, false, false);
        expect(d.path == AdaptivePath::server_preedit,
               "atomic + stale + server preedit → server_preedit");
    }

    // uinput + client preedit → client_preedit (never direct)
    {
        const auto d = AdaptiveRouter::route(uinputWithClientPreedit(), false, false);
        expect(d.path == AdaptivePath::client_preedit,
               "uinput + client preedit → client_preedit");
        expect(d.reason == RouteReason::no_atomic_client_preedit,
               "uinput reason");
    }

    // uinput + server preedit → server_preedit
    {
        const auto d = AdaptiveRouter::route(uinputWithServerPreedit(), false, false);
        expect(d.path == AdaptivePath::server_preedit,
               "uinput + server preedit → server_preedit");
    }

    // uinput + no preedit → passthrough
    {
        const auto d = AdaptiveRouter::route(uinputNoPreedit(), false, false);
        expect(d.path == AdaptivePath::passthrough,
               "uinput + no preedit → passthrough");
    }

    // no capabilities → passthrough
    {
        const auto d = AdaptiveRouter::route(noCapability(), false, false);
        expect(d.path == AdaptivePath::passthrough,
               "no capability → passthrough");
        expect(d.reason == RouteReason::no_capability,
               "no capability reason");
    }

    // no atomic + client preedit → client_preedit
    {
        const auto d = AdaptiveRouter::route(noAtomicClientPreedit(), false, false);
        expect(d.path == AdaptivePath::client_preedit,
               "no atomic + client preedit → client_preedit");
    }

    // no atomic + server preedit → server_preedit
    {
        const auto d = AdaptiveRouter::route(noAtomicServerPreedit(), false, false);
        expect(d.path == AdaptivePath::server_preedit,
               "no atomic + server preedit → server_preedit");
    }
}

void testDisabled()
{
    const auto d = AdaptiveRouter::route(atomicValid(), false, true);
    expect(d.path == AdaptivePath::passthrough,
           "disabled → passthrough");
    expect(d.reason == RouteReason::disabled,
           "disabled reason");
}

void testQuarantine()
{
    // quarantined + client preedit → client_preedit
    {
        const auto d = AdaptiveRouter::route(atomicValid(), true, false);
        expect(d.path == AdaptivePath::client_preedit,
               "quarantined + client preedit → client_preedit");
        expect(d.reason == RouteReason::quarantined,
               "quarantined reason");
    }

    // quarantined + server preedit → server_preedit
    {
        InputCapabilities caps = atomicValid();
        caps.preedit = PreeditSemantics::server;
        const auto d = AdaptiveRouter::route(caps, true, false);
        expect(d.path == AdaptivePath::server_preedit,
               "quarantined + server preedit → server_preedit");
    }

    // quarantined + no preedit → passthrough
    {
        const auto d = AdaptiveRouter::route(uinputNoPreedit(), true, false);
        expect(d.path == AdaptivePath::passthrough,
               "quarantined + no preedit → passthrough");
    }
}

void testStickiness()
{
    const RouteDecision current = {AdaptivePath::atomic_direct,
                                    RouteReason::atomic_available, 100};

    // Better capability appears — should hold route
    {
        InputCapabilities better = atomicValid();
        better.signature = 999;
        expect(AdaptiveRouter::shouldHoldRoute(current, better),
               "should hold route when better capability appears");
    }

    // Passthrough should not hold
    {
        const RouteDecision passthrough = {AdaptivePath::passthrough,
                                            RouteReason::no_capability, 300};
        expect(!AdaptiveRouter::shouldHoldRoute(passthrough, atomicValid()),
               "passthrough should not hold");
    }
}

void testFence()
{
    const RouteDecision direct = {AdaptivePath::atomic_direct,
                                   RouteReason::atomic_available, 100};

    // Atomic capability lost → fence
    {
        InputCapabilities lost = noAtomicClientPreedit();
        expect(AdaptiveRouter::requiresFence(direct, lost),
               "atomic lost → fence");
    }

    // Atomic still available → no fence
    {
        expect(!AdaptiveRouter::requiresFence(direct, atomicValid()),
               "atomic available → no fence");
    }

    // Client preedit lost → fence
    {
        const RouteDecision preedit = {AdaptivePath::client_preedit,
                                        RouteReason::no_atomic_client_preedit, 301};
        InputCapabilities lost = noCapability();
        expect(AdaptiveRouter::requiresFence(preedit, lost),
               "client preedit lost → fence");
    }

    // Passthrough never fences
    {
        const RouteDecision passthrough = {AdaptivePath::passthrough,
                                            RouteReason::no_capability, 300};
        expect(!AdaptiveRouter::requiresFence(passthrough, noCapability()),
               "passthrough never fences");
    }
}

void testHealthRegistry()
{
    RouteHealthRegistry registry;
    const RouteHealthKey key{42, "wayland", ":0",
                             ReplacementSemantics::protocol_transaction, 100};

    // Initially not quarantined
    expect(!registry.isQuarantined(key), "initial not quarantined");
    expect(registry.size() == 0, "initial size 0");

    // Quarantine
    registry.quarantine(key, 1);
    expect(registry.isQuarantined(key), "quarantined after quarantine");
    expect(registry.size() == 1, "size 1 after quarantine");

    const auto h = registry.health(key);
    expect(h.quarantined, "health quarantined flag");
    expect(h.quarantine_generation == 1, "health generation");

    // Different key not quarantined
    const RouteHealthKey key2{42, "wayland", ":0",
                              ReplacementSemantics::protocol_transaction, 200};
    expect(!registry.isQuarantined(key2),
           "different signature not quarantined");

    // Clear specific key
    registry.clear(key);
    expect(!registry.isQuarantined(key), "cleared after clear");
    expect(registry.size() == 0, "size 0 after clear");

    // Clear all
    registry.quarantine(key, 2);
    registry.quarantine(key2, 3);
    expect(registry.size() == 2, "size 2 before clearAll");
    registry.clearAll();
    expect(registry.size() == 0, "size 0 after clearAll");
}

void testClientAtomicEvent()
{
    // client_atomic_event should also be treated as atomic
    InputCapabilities caps = {ReplacementSemantics::client_atomic_event,
                              PreeditSemantics::client,
                              true, true, true, 1, 400};
    const auto d = AdaptiveRouter::route(caps, false, false);
    expect(d.path == AdaptivePath::atomic_direct,
           "client_atomic_event → atomic_direct");
}

void testExpandedSelection()
{
    // Atomic but selection expanded → preedit fallback
    InputCapabilities caps = atomicValid();
    caps.selection_collapsed = false;
    const auto d = AdaptiveRouter::route(caps, false, false);
    expect(d.path == AdaptivePath::client_preedit,
           "expanded selection → preedit");
}

} // namespace

int main()
{
    testRouting();
    testDisabled();
    testQuarantine();
    testStickiness();
    testFence();
    testHealthRegistry();
    testClientAtomicEvent();
    testExpandedSelection();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All adaptive router tests passed\n";
    return 0;
}
