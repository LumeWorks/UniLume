// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings_model.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace unilume::config_gui {

inline constexpr std::size_t max_backup_bytes = 16 * 1024 * 1024;

struct BackupDecodeResult {
    Settings settings;
    bool migrated{};
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] std::string encodeBackup(const Settings &settings);
[[nodiscard]] BackupDecodeResult decodeBackup(std::string_view text);

} // namespace unilume::config_gui
