// SPDX-License-Identifier: GPL-2.0-or-later

#include <fcitx-utils/i18n.h>

#include <clocale>
#include <iostream>
#include <string>

int main()
{
    if (std::setlocale(LC_ALL, "") == nullptr) {
        std::cerr << "requested test locale is unavailable\n";
        return 1;
    }
    fcitx::registerDomain("unilume", UNILUME_TEST_LOCALE_DIR);
    const std::string translated = _("Direct - Fast");
    if (translated != "Trực tiếp - Nhanh") {
        std::cerr << "Vietnamese catalog was not loaded: "
                  << translated << '\n';
        return 1;
    }
    const std::string gui_translated =
        _("Open the complete UniLume configuration");
    if (gui_translated != "Mở toàn bộ cấu hình UniLume") {
        std::cerr << "Vietnamese GUI catalog was not loaded: "
                  << gui_translated << '\n';
        return 1;
    }
    return 0;
}
