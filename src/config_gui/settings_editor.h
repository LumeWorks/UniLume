// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "settings_model.h"

#include <QWidget>

#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

namespace unilume::config_gui {

class SettingsEditor final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsEditor(QWidget *parent = nullptr);

    void setSettings(const Settings &settings);
    [[nodiscard]] Settings settings() const;
    [[nodiscard]] bool valid() const;
    void focusField(const std::string &field);
    void setBackupPreview(const QString &preview);

signals:
    void changed();
    void validityChanged(bool valid);
    void importRequested();
    void exportRequested();

private:
    QWidget *createScalarPage(Category category);
    QWidget *createResourcePage(Category category,
                                const char *enabled_key,
                                const QString &accessible_name,
                                const QString &help,
                                QPlainTextEdit **editor);
    QWidget *createAppearancePage();
    QWidget *createBackupPage();
    void handleChanged();
    void updateValidation();

    QTabWidget *tabs_{};
    QLabel *validation_label_{};
    QLabel *backup_preview_{};
    std::map<std::string, QWidget *> controls_;
    QPlainTextEdit *macro_editor_{};
    QPlainTextEdit *dictionary_editor_{};
    QPlainTextEdit *keymap_editor_{};
    QPlainTextEdit *application_editor_{};
    Settings baseline_;
    bool loading_{};
    bool valid_{};
};

} // namespace unilume::config_gui
