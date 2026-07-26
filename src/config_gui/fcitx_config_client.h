// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings_model.h"

#include <string>

namespace unilume::config_gui {

class FcitxConfigClient final {
public:
    FcitxConfigClient();
    ~FcitxConfigClient();

    FcitxConfigClient(const FcitxConfigClient &) = delete;
    FcitxConfigClient &operator=(const FcitxConfigClient &) = delete;

    [[nodiscard]] bool available(std::string *error = nullptr) const;
    [[nodiscard]] bool load(Settings &settings,
                            std::string *error = nullptr) const;
    [[nodiscard]] bool apply(const Settings &settings,
                             std::string *error = nullptr) const;

private:
    class Implementation;
    Implementation *implementation_;
};

} // namespace unilume::config_gui
