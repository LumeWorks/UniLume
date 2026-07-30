// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_mode_policy.h"
#include "test_assertions.h"
#include "test_suites.h"

namespace unilume::integration::test {

void runInputModePolicyTests(Assertions &assertions)
{
    // -- Direct fails open to raw passthrough when unavailable --
    platform::InputModePolicy unavailable_at_start;
    unavailable_at_start.observe(false);
    assertions.truth(
        "unavailable direct backend selects passthrough",
        unavailable_at_start.path() == platform::InputPath::off);
    unavailable_at_start.observe(true);
    assertions.truth(
        "restored direct backend is selected immediately",
        unavailable_at_start.path() == platform::InputPath::direct);

    // -- Between compositions, re-evaluation can upgrade --
    unavailable_at_start.resetForCompositionEnd();
    unavailable_at_start.observe(true);
    assertions.truth(
        "between compositions, restored capability selects direct",
        unavailable_at_start.path() == platform::InputPath::direct);

    // -- Available capability selects direct path initially --
    platform::InputModePolicy direct_at_start;
    direct_at_start.observe(true);
    assertions.truth(
        "available capability selects direct path initially",
        direct_at_start.path() == platform::InputPath::direct);

    // -- Capability loss demotes to passthrough and can re-promote --
    direct_at_start.observe(false);
    assertions.truth(
        "capability loss returns to passthrough",
        direct_at_start.path() == platform::InputPath::off);
    direct_at_start.observe(true);
    assertions.truth(
        "capability restoration selects direct without preedit",
        direct_at_start.path() == platform::InputPath::direct);

    // -- Between compositions, re-evaluation re-promotes --
    direct_at_start.resetForCompositionEnd();
    direct_at_start.observe(true);
    assertions.truth(
        "next composition selects direct after restoration",
        direct_at_start.path() == platform::InputPath::direct);

    // -- Full reset clears the path to unknown --
    platform::InputModePolicy reset_test;
    reset_test.observe(true);
    assertions.truth("first observation direct",
        reset_test.path() == platform::InputPath::direct);
    reset_test.reset();
    assertions.truth("reset restores unknown path",
        reset_test.path() == platform::InputPath::unknown);
    reset_test.observe(false);
    assertions.truth("after reset, unavailable backend picks passthrough",
        reset_test.path() == platform::InputPath::off);

    platform::InputModePolicy explicit_modes;
    explicit_modes.observe(policy::ApplicationMode::safe_preedit, true);
    assertions.truth(
        "legacy safe preedit maps to off",
        explicit_modes.path() == platform::InputPath::off);
    explicit_modes.observe(policy::ApplicationMode::off, true);
    assertions.truth(
        "off mode bypasses both processing paths",
        explicit_modes.path() == platform::InputPath::off);
    explicit_modes.observe(policy::ApplicationMode::direct, false);
    assertions.truth(
        "explicit direct mode passes through when capability is unavailable",
        explicit_modes.path() == platform::InputPath::off);

    explicit_modes.observe(policy::ApplicationMode::automatic, true);
    assertions.truth(
        "legacy automatic maps to direct",
        explicit_modes.path() == platform::InputPath::direct);
    explicit_modes.resetForCompositionEnd();
    explicit_modes.observe(policy::ApplicationMode::direct, true);
    assertions.truth(
        "explicit direct mode uses approved backend when available",
        explicit_modes.path() == platform::InputPath::direct);
}

} // namespace unilume::integration::test
