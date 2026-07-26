// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_mode_policy.h"
#include "test_assertions.h"
#include "test_suites.h"

namespace unilume::integration::test {

void runInputModePolicyTests(Assertions &assertions)
{
    // -- Unknown starts on preedit when capability unavailable --
    platform::InputModePolicy unavailable_at_start;
    unavailable_at_start.observe(false);
    unavailable_at_start.observe(true);
    assertions.truth(
        "context does not promote after starting on preedit",
        unavailable_at_start.path() == platform::InputPath::preedit);

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

    // -- Capability loss demotes but does not re-promote during composition --
    direct_at_start.observe(false);
    assertions.truth(
        "capability loss returns to preedit",
        direct_at_start.path() == platform::InputPath::preedit);
    direct_at_start.observe(true);
    assertions.truth(
        "capability restored does not re-promote same composition",
        direct_at_start.path() == platform::InputPath::preedit);

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
    assertions.truth("after reset, new observation picks preedit",
        reset_test.path() == platform::InputPath::preedit);

    platform::InputModePolicy explicit_modes;
    explicit_modes.observe(policy::ApplicationMode::safe_preedit, true);
    assertions.truth(
        "safe preedit never selects direct replacement",
        explicit_modes.path() == platform::InputPath::preedit);
    explicit_modes.observe(policy::ApplicationMode::off, true);
    assertions.truth(
        "off mode bypasses both processing paths",
        explicit_modes.path() == platform::InputPath::off);
    explicit_modes.observe(policy::ApplicationMode::direct, false);
    assertions.truth(
        "explicit direct mode falls back when capability is unavailable",
        explicit_modes.path() == platform::InputPath::preedit);
    explicit_modes.resetForCompositionEnd();
    explicit_modes.observe(policy::ApplicationMode::direct, true);
    assertions.truth(
        "explicit direct mode uses approved backend when available",
        explicit_modes.path() == platform::InputPath::direct);
}

} // namespace unilume::integration::test
