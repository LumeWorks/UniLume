// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace unilume::platform {

// Semantic classification of the surrounding-text replacement transport.
//
//   none                 - no replacement primitive is available.
//
//   split_unverified     - the transport delivers delete and insert as
//                          separate messages with no ordering or atomicity
//                          guarantee.  uinput, D-Bus split and any
//                          delete-then-commit sequence belong here.  A
//                          synthetic ACK does not prove the target
//                          application applied the full replacement.
//
//   client_atomic_event  - the transport delivers a single client-side
//                          event that atomically replaces a range, e.g.
//                          Qt QInputMethodEvent with setCommitString.
//
//   protocol_transaction - the transport wraps delete and insert in one
//                          protocol-level transaction with a serial or
//                          commit, e.g. Wayland text-input v2.
enum class ReplacementSemantics {
    none,
    split_unverified,
    client_atomic_event,
    protocol_transaction,
};

// Semantic classification of the preedit path.
//
//   none   - no preedit support.
//
//   server - the input panel preedit works via updateUserInterface.
//
//   client - the client advertises preedit capability and
//            updateClientPreedit works.
enum class PreeditSemantics {
    none,
    server,
    client,
};

// Aggregated capability snapshot observed at composition start.
//
// `signature` distinguishes capability changes within the same input
// context, allowing the health registry to re-evaluate after a transport
// or frontend change.
struct InputCapabilities {
    ReplacementSemantics replacement{ReplacementSemantics::none};
    PreeditSemantics preedit{PreeditSemantics::none};

    bool surrounding_text{};
    bool surrounding_snapshot_valid{};
    bool selection_collapsed{};

    std::uint64_t generation{};
    std::uint64_t signature{};

    [[nodiscard]] bool atomicReplacement() const
    {
        return replacement == ReplacementSemantics::client_atomic_event ||
               replacement == ReplacementSemantics::protocol_transaction;
    }

    [[nodiscard]] bool anyPreedit() const
    {
        return preedit != PreeditSemantics::none;
    }
};

} // namespace unilume::platform
