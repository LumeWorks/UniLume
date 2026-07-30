// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_mode_policy.h"

namespace unilume::platform {

InputPath InputModePolicy::observe(bool direct_available)
{
    return observe(policy::ApplicationMode::direct, direct_available);
}

InputPath InputModePolicy::observe(policy::ApplicationMode requested,
                                   bool direct_available)
{
    requested = policy::normalizeMode(requested);
    if (requested != requested_) {
        path_ = InputPath::unknown;
        requested_ = requested;
    }
    if (requested == policy::ApplicationMode::off) {
        path_ = InputPath::off;
        return path_;
    }
    path_ = direct_available ? InputPath::direct : InputPath::off;
    return path_;
}

void InputModePolicy::resetForCompositionEnd()
{
    // Between compositions, reset to unknown so the next composition
    // re-evaluates the path based on current frontend capability.
    path_ = InputPath::unknown;
}

void InputModePolicy::reset()
{
    path_ = InputPath::unknown;
    requested_ = policy::ApplicationMode::direct;
}

InputPath InputModePolicy::path() const
{
    return path_;
}

policy::ApplicationMode InputModePolicy::requestedMode() const
{
    return requested_;
}

} // namespace unilume::platform
