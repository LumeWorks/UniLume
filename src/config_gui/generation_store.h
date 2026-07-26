// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings_model.h"

#include <filesystem>
#include <string>

namespace unilume::config_gui {

struct StageResult {
    Settings settings;
    std::filesystem::path generation;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

class GenerationStore final {
public:
    explicit GenerationStore(std::filesystem::path root);

    [[nodiscard]] StageResult stage(const Settings &source) const;
    [[nodiscard]] bool discard(
        const std::filesystem::path &generation,
        std::string *error = nullptr) const;
    void collect(const std::filesystem::path &active_generation,
                 std::size_t keep = 2) const;

    [[nodiscard]] const std::filesystem::path &root() const
    {
        return root_;
    }

private:
    [[nodiscard]] bool owns(
        const std::filesystem::path &generation) const;

    std::filesystem::path root_;
};

} // namespace unilume::config_gui
