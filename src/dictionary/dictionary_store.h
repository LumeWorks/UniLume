// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "dictionary_contract.h"

#include <filesystem>
#include <string>

namespace unilume::dictionary {

enum class LoadDisposition
{
    loaded,
    missing,
    rejected
};

struct LoadResult
{
    LoadDisposition disposition{LoadDisposition::missing};
    Snapshot snapshot;
    std::string error;

    [[nodiscard]] bool ok() const
    {
        return disposition != LoadDisposition::rejected;
    }
};

class Store final
{
  public:
    explicit Store(std::filesystem::path path);

    [[nodiscard]] LoadResult load();
    [[nodiscard]] bool save(const Snapshot &snapshot,
                            std::string *error = nullptr) const;
    [[nodiscard]] const Snapshot &active() const { return active_; }

  private:
    std::filesystem::path path_;
    Snapshot active_;
};

} // namespace unilume::dictionary
