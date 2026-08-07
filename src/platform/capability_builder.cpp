// SPDX-License-Identifier: GPL-2.0-or-later

#include "capability_builder.h"

namespace unilume::platform {

std::uint64_t computeCapabilitySignature(
    ReplacementSemantics replacement,
    PreeditSemantics preedit,
    bool surrounding_text)
{
    std::uint64_t signature = 0;
    signature =
        (signature << 8) |
        static_cast<std::uint64_t>(replacement);
    signature =
        (signature << 8) |
        static_cast<std::uint64_t>(preedit);
    signature =
        (signature << 8) |
        static_cast<std::uint64_t>(surrounding_text ? 1 : 0);
    return signature;
}

InputCapabilities buildCapabilities(
    ReplacementSemantics replacement,
    PreeditSemantics preedit,
    bool surrounding_text,
    bool surrounding_snapshot_valid,
    bool selection_collapsed,
    std::uint64_t generation)
{
    InputCapabilities caps;
    caps.replacement = replacement;
    caps.preedit = preedit;
    caps.surrounding_text = surrounding_text;
    caps.surrounding_snapshot_valid = surrounding_snapshot_valid;
    caps.selection_collapsed = selection_collapsed;
    caps.generation = generation;
    caps.signature =
        computeCapabilitySignature(replacement, preedit, surrounding_text);
    return caps;
}

} // namespace unilume::platform
