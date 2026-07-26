// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "config_snapshot.h"
#include "unilume_context.h"

namespace unilume::core {

// This is the sole mapping from the versioned application configuration to
// the inherited UniKey option struct. Adapters must not assign legacy fields.
[[nodiscard]] UlEngineOptions engineOptionsFromSnapshot(
    const config::Snapshot &snapshot);

} // namespace unilume::core
