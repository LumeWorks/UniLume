// SPDX-License-Identifier: GPL-2.0-or-later

#include "engine_options.h"

namespace unilume::core {

UlEngineOptions engineOptionsFromSnapshot(const config::Snapshot &snapshot)
{
    return {
        snapshot.spell_check ? 1 : 0,
        snapshot.free_marking ? 1 : 0,
        snapshot.modern_tone ? 1 : 0,
        snapshot.auto_restore ? 1 : 0,
    };
}

} // namespace unilume::core
