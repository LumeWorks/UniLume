// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "capability_model.h"

#include <cstdint>

namespace unilume::platform {

// Build an InputCapabilities snapshot from already-resolved semantics.
//
// The capability signature is derived from the stable capability dimensions
// (transport semantics, preedit semantics, surrounding-text capability) and
// deliberately excludes surrounding_snapshot_valid, selection_collapsed,
// cursor state and generation.  A stale snapshot or a generation bump must
// not change the signature, so the health registry keeps quarantine across
// those transient changes and only re-evaluates when the real capability
// (transport/preedit/frontend capability) changes.
[[nodiscard]] InputCapabilities buildCapabilities(
    ReplacementSemantics replacement,
    PreeditSemantics preedit,
    bool surrounding_text,
    bool surrounding_snapshot_valid,
    bool selection_collapsed,
    std::uint64_t generation);

// Compute the stable capability signature from the three capability
// dimensions.  Used by buildCapabilities and by the health registry to
// distinguish capability changes within the same input context.
[[nodiscard]] std::uint64_t computeCapabilitySignature(
    ReplacementSemantics replacement,
    PreeditSemantics preedit,
    bool surrounding_text);

} // namespace unilume::platform
