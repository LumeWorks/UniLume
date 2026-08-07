// SPDX-License-Identifier: GPL-2.0-or-later

#include "capability_builder.h"

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

void testSignatureStability()
{
    // The signature must NOT depend on snapshot validity, selection, or
    // generation: a stale snapshot or a generation bump must not change it.
    const std::uint64_t base = computeCapabilitySignature(
        ReplacementSemantics::client_atomic_event,
        PreeditSemantics::client,
        true);

    expect(base == computeCapabilitySignature(
                      ReplacementSemantics::client_atomic_event,
                      PreeditSemantics::client,
                      true),
           "signature stable across calls");

    // Surrounding-text capability is part of the signature: losing it
    // changes the signature (real capability change clears quarantine).
    const std::uint64_t no_surrounding = computeCapabilitySignature(
        ReplacementSemantics::client_atomic_event,
        PreeditSemantics::client,
        false);
    expect(no_surrounding != base,
           "losing surrounding text changes signature");
}

void testBuildCapabilitiesFields()
{
    const InputCapabilities caps = buildCapabilities(
        ReplacementSemantics::protocol_transaction,
        PreeditSemantics::server,
        true,   // surrounding_text
        false,  // snapshot valid
        true,   // selection collapsed
        7);     // generation

    expect(caps.replacement == ReplacementSemantics::protocol_transaction,
           "replacement field");
    expect(caps.preedit == PreeditSemantics::server,
           "preedit field");
    expect(caps.surrounding_text, "surrounding_text field");
    expect(!caps.surrounding_snapshot_valid, "snapshot_valid field");
    expect(caps.selection_collapsed, "selection_collapsed field");
    expect(caps.generation == 7, "generation field");
    expect(caps.atomicReplacement(), "atomicReplacement helper");
    expect(caps.anyPreedit(), "anyPreedit helper");
}

void testSignatureExcludesTransientState()
{
    // Two snapshots with identical capability dimensions but different
    // snapshot validity / selection / generation share a signature.
    const InputCapabilities a = buildCapabilities(
        ReplacementSemantics::client_atomic_event,
        PreeditSemantics::client,
        true, true, true, 1);
    const InputCapabilities b = buildCapabilities(
        ReplacementSemantics::client_atomic_event,
        PreeditSemantics::client,
        true, false, false, 99);
    expect(a.signature == b.signature,
           "signature excludes snapshot/selection/generation");
}

void testSignatureDistinguishesSemantics()
{
    const std::uint64_t atomic_client =
        computeCapabilitySignature(
            ReplacementSemantics::client_atomic_event,
            PreeditSemantics::client,
            true);
    const std::uint64_t split_client =
        computeCapabilitySignature(
            ReplacementSemantics::split_unverified,
            PreeditSemantics::client,
            true);
    const std::uint64_t atomic_server =
        computeCapabilitySignature(
            ReplacementSemantics::client_atomic_event,
            PreeditSemantics::server,
            true);
    const std::uint64_t none_none =
        computeCapabilitySignature(
            ReplacementSemantics::none,
            PreeditSemantics::none,
            false);

    expect(atomic_client != split_client,
           "atomic vs split distinguished");
    expect(atomic_client != atomic_server,
           "client vs server preedit distinguished");
    expect(atomic_client != none_none,
           "atomic vs none distinguished");
    expect(split_client != none_none,
           "split vs none distinguished");
}

} // namespace

int main()
{
    testSignatureStability();
    testBuildCapabilitiesFields();
    testSignatureExcludesTransientState();
    testSignatureDistinguishesSemantics();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All capability builder tests passed\n";
    return 0;
}
