// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_mode_policy.h"

namespace unilume::platform {

InputPath InputModePolicy::observe(bool direct_available)
{
    return observe(policy::ApplicationMode::automatic, direct_available);
}

InputPath InputModePolicy::observe(policy::ApplicationMode requested,
                                   bool direct_available)
{
    if (requested != requested_) {
        path_ = InputPath::unknown;
        requested_ = requested;
    }
    if (requested == policy::ApplicationMode::off) {
        path_ = InputPath::off;
        return path_;
    }
    if (requested == policy::ApplicationMode::safe_preedit) {
        path_ = InputPath::preedit;
        return path_;
    }
    if (requested == policy::ApplicationMode::direct) {
        path_ = direct_available ? InputPath::direct : InputPath::off;
        return path_;
    }
    // Automatic owns one path for the lifetime of a composition. Promoting
    // preedit to direct halfway through a word would split the text across
    // two transports and can reorder or duplicate characters in clients.
    if (path_ == InputPath::preedit) {
        return path_;
    }
    if (path_ == InputPath::direct && !direct_available) {
        path_ = InputPath::preedit;
        return path_;
    }
    if (path_ == InputPath::unknown) {
        path_ = direct_available ? InputPath::direct : InputPath::preedit;
    }
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
    requested_ = policy::ApplicationMode::automatic;
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
