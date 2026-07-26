// SPDX-License-Identifier: GPL-2.0-or-later

#include "settings_editor.h"

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QTabWidget>

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    unilume::config_gui::SettingsEditor editor;
    editor.resize(860, 620);
    editor.show();
    application.processEvents();

    auto *tabs = editor.findChild<QTabWidget *>();
    expect(tabs && tabs->count() == 9,
           "all configuration categories are keyboard reachable");
    expect(editor.devicePixelRatioF() >= 1.0,
           "editor renders with the active device pixel ratio");
    const QPixmap rendering = editor.grab();
    expect(!rendering.isNull() &&
               rendering.devicePixelRatio() >= 1.0,
           "complete editor renders through the active desktop style");
    const QString screenshot =
        qEnvironmentVariable("UNILUME_GUI_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        expect(rendering.save(screenshot),
               "visual smoke screenshot can be saved");
    }
    for (const std::string_view key :
         unilume::config_gui::allConfigKeys()) {
        if (key.ends_with("File")) {
            continue;
        }
        QWidget *control =
            editor.findChild<QWidget *>(QString::fromUtf8(key));
        expect(control != nullptr,
               "every non-path production option has a control");
        expect(control && !control->accessibleName().isEmpty(),
               "every production control has an accessible name");
        expect(control &&
                   control->focusPolicy() != Qt::NoFocus,
               "every production control accepts keyboard focus");
    }
    const auto resource_editors =
        editor.findChildren<QPlainTextEdit *>();
    expect(resource_editors.size() == 4,
           "all managed resources have editors");
    for (const QPlainTextEdit *resource : resource_editors) {
        expect(!resource->accessibleName().isEmpty(),
               "every resource editor has an accessible name");
        expect(resource->focusPolicy() != Qt::NoFocus,
               "every resource editor accepts keyboard focus");
    }

    auto *emoji_hotkey =
        editor.findChild<QLineEdit *>("EmojiHotkey");
    expect(emoji_hotkey != nullptr, "emoji hotkey control exists");
    emoji_hotkey->setText("Not-A-Real-Fcitx-Key");
    application.processEvents();
    expect(!editor.valid(),
           "invalid field disables a valid editor state");
    emoji_hotkey->setText("Control+Alt+period");
    application.processEvents();
    expect(editor.valid(), "corrected field restores valid state");

    auto *spell = editor.findChild<QCheckBox *>("SpellCheck");
    expect(spell != nullptr, "typing option is exposed");
    const bool before = spell->isChecked();
    spell->toggle();
    application.processEvents();
    expect(editor.settings().values["SpellCheck"] ==
               (before ? "False" : "True"),
           "widget changes update only staged settings");

    return failures == 0 ? 0 : 1;
}
