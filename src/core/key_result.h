// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace unilume::core {

struct KeyResult {
    bool handled{};
    std::uint64_t sequence_id{};
    std::int32_t delete_before_cursor{};
    std::string_view commit_text;
    std::string_view preedit_text;
    bool reset_context{};
    bool require_fallback{};
    bool commit_preedit_before{};
    bool defer_preedit_commit{};
};

} // namespace unilume::core
