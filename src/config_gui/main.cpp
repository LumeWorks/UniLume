// SPDX-License-Identifier: GPL-2.0-or-later

#include "backup_codec.h"
#include "fcitx_config_client.h"
#include "generation_store.h"
#include "main_window.h"

#include <fcitx-utils/i18n.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QStandardPaths>

#include <filesystem>
#include <iostream>

namespace {

std::filesystem::path generationRoot()
{
    return (std::filesystem::path(
                QStandardPaths::writableLocation(
                    QStandardPaths::GenericDataLocation)
                    .toStdString()) /
            "fcitx5" / "unilume" / "config-generations");
}

int integrationSmoke()
{
    unilume::config_gui::FcitxConfigClient client;
    unilume::config_gui::Settings current;
    std::string error;
    if (!client.load(current, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    unilume::config_gui::GenerationStore generations(generationRoot());
    unilume::config_gui::StageResult staged =
        generations.stage(current);
    if (!staged.ok()) {
        std::cerr << staged.error << '\n';
        return 1;
    }
    if (!client.apply(staged.settings, &error)) {
        std::string ignored;
        const bool removed =
            generations.discard(staged.generation, &ignored);
        (void)removed;
        std::cerr << error << '\n';
        return 1;
    }
    generations.collect(staged.generation);
    std::cout << "UniLume configuration D-Bus smoke passed\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    fcitx::registerDomain("unilume", UNILUME_LOCALE_DIR);
    application.setApplicationName("unilume-config");
    application.setApplicationDisplayName(_("UniLume Configuration"));
    application.setOrganizationDomain("unilume.org");
    application.setDesktopFileName(
        "org.unilume.UniLume.Configuration");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        _("Configure the UniLume Fcitx5 input method."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption integration_smoke(
        "integration-smoke",
        _("Round-trip the live Fcitx configuration and exit."));
    parser.addOption(integration_smoke);
    parser.process(application);
    if (parser.isSet(integration_smoke)) {
        return integrationSmoke();
    }

    unilume::config_gui::MainWindow window;
    window.show();
    return application.exec();
}
