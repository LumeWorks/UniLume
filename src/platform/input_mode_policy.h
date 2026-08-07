// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "application_policy.h"

namespace unilume::platform {

enum class InputPath {
    unknown,
    direct,
    preedit,
    off,
};

class InputModePolicy {
public:
    InputPath observe(bool direct_available);
    InputPath observe(policy::ApplicationMode requested,
                      bool direct_available);
    // Set the path directly.  Used by the adaptive router, which owns
    // composition stickiness itself; observe()'s internal stickiness does
    // not apply when the router drives the decision.
    void assignPath(InputPath path, policy::ApplicationMode requested);
    void resetForCompositionEnd();
    void reset();
    [[nodiscard]] InputPath path() const;
    [[nodiscard]] policy::ApplicationMode requestedMode() const;

private:
    InputPath path_{InputPath::unknown};
    policy::ApplicationMode requested_{policy::ApplicationMode::adaptive};
};

} // namespace unilume::platform
