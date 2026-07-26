// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "fcitx_config_client.h"
#include "generation_store.h"

#include <QMainWindow>

class QCloseEvent;
class QPushButton;

namespace unilume::config_gui {

class SettingsEditor;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void apply();
    void cancel();
    void reset();
    void importBackup();
    void exportBackup();
    void updateButtons();

private:
    [[nodiscard]] bool load();
    [[nodiscard]] bool confirmDiscard();
    void showFailure(const QString &title, const std::string &error);

    SettingsEditor *editor_{};
    QPushButton *apply_button_{};
    QPushButton *reset_button_{};
    QPushButton *cancel_button_{};
    FcitxConfigClient client_;
    GenerationStore generations_;
    Settings applied_;
    bool loaded_{};
    bool applying_{};
};

} // namespace unilume::config_gui
