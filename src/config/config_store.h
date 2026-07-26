// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "config_snapshot.h"

#include <filesystem>
#include <string>

namespace unilume::config {

enum class LoadDisposition { loaded, migrated, missing, rejected };

struct LoadResult {
    LoadDisposition disposition{LoadDisposition::missing};
    Snapshot snapshot{};
    std::string error;

    [[nodiscard]] bool ok() const { return disposition != LoadDisposition::rejected; }
};

class Store final {
public:
    explicit Store(std::filesystem::path path);

    [[nodiscard]] LoadResult load();
    [[nodiscard]] bool save(const Snapshot &snapshot, std::string *error = nullptr) const;
    [[nodiscard]] bool reset(std::string *error = nullptr);
    [[nodiscard]] const Snapshot &active() const { return active_; }
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
    Snapshot active_{defaults()};
};

} // namespace unilume::config
