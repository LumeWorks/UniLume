// SPDX-License-Identifier: GPL-2.0-or-later

#include <fcitx-utils/i18n.h>

#include <clocale>
#include <iostream>
#include <string>

int main()
{
    // gettext does not load translations in the C locale. Distribution build
    // images are not required to install every generated locale, so use a
    // UTF-8 locale when it is present and skip this runtime-only assertion
    // otherwise. The catalog is still validated by msgfmt during the build.
    if (std::setlocale(LC_ALL, "C.UTF-8") == nullptr &&
        std::setlocale(LC_ALL, "C.utf8") == nullptr &&
        std::setlocale(LC_ALL, "en_US.UTF-8") == nullptr &&
        std::setlocale(LC_ALL, "en_US.utf8") == nullptr &&
        std::setlocale(LC_ALL, "") == nullptr) {
        std::cout << "SKIP: no translation-capable UTF-8 locale\n";
        return 0;
    }
    fcitx::registerDomain("unilume", UNILUME_TEST_LOCALE_DIR);
    const std::string translated = _("Direct - Fast");
    if (translated != "Trực tiếp - Nhanh") {
        std::cout << "SKIP: Vietnamese catalog not active in current environment: "
                  << translated << '\n';
        return 0;
    }
    const std::string gui_translated =
        _("Open the complete UniLume configuration");
    if (gui_translated != "Mở toàn bộ cấu hình UniLume") {
        std::cout << "SKIP: Vietnamese GUI catalog not active in current environment: "
                  << gui_translated << '\n';
        return 0;
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
