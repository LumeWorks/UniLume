// SPDX-License-Identifier: GPL-2.0-or-later

#include "main_window.h"

#include "backup_codec.h"
#include "settings_editor.h"

#include <fcitx-utils/i18n.h>

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>

namespace unilume::config_gui {
namespace {

std::filesystem::path generationRoot()
{
    return (std::filesystem::path(
                QStandardPaths::writableLocation(
                    QStandardPaths::GenericDataLocation)
                    .toStdString()) /
            "fcitx5" / "unilume" / "config-generations");
}

QString backupSummary(const Settings &settings, bool migrated)
{
    const auto lines = [](const std::string &text) {
        return std::count(text.begin(), text.end(), '\n');
    };
    return QString(
               _("Preview: %1 options, %2 macro lines, %3 dictionary lines, "
                 "%4 keymap lines and %5 application-policy lines.%6"))
        .arg(settings.values.size())
        .arg(lines(settings.resources.macros))
        .arg(lines(settings.resources.dictionary))
        .arg(lines(settings.resources.keymap))
        .arg(lines(settings.resources.application_policy))
        .arg(migrated ? _(" Older backup values were migrated to current "
                         "defaults.")
                      : "");
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      editor_(new SettingsEditor(this)),
      generations_(generationRoot())
{
    setWindowTitle(_("UniLume Configuration"));
    setWindowIcon(QIcon::fromTheme("unilume"));
    resize(880, 680);
    setMinimumSize(680, 520);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(editor_, 1);
    auto *buttons = new QDialogButtonBox;
    apply_button_ = buttons->addButton(
        _("Apply"), QDialogButtonBox::ApplyRole);
    reset_button_ = buttons->addButton(
        _("Reset"), QDialogButtonBox::ResetRole);
    cancel_button_ = buttons->addButton(
        _("Cancel"), QDialogButtonBox::RejectRole);
    apply_button_->setAccessibleName(_("Apply configuration"));
    reset_button_->setAccessibleName(_("Reset to defaults"));
    cancel_button_->setAccessibleName(_("Cancel without applying"));
    layout->addWidget(buttons);
    setCentralWidget(central);

    connect(apply_button_, &QPushButton::clicked,
            this, &MainWindow::apply);
    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset);
    connect(cancel_button_, &QPushButton::clicked,
            this, &MainWindow::cancel);
    connect(editor_, &SettingsEditor::changed,
            this, &MainWindow::updateButtons);
    connect(editor_, &SettingsEditor::validityChanged,
            this, &MainWindow::updateButtons);
    connect(editor_, &SettingsEditor::importRequested,
            this, &MainWindow::importBackup);
    connect(editor_, &SettingsEditor::exportRequested,
            this, &MainWindow::exportBackup);

    loaded_ = load();
    updateButtons();
}

bool MainWindow::load()
{
    std::string error;
    if (!client_.load(applied_, &error)) {
        applied_ = defaultSettings();
        editor_->setSettings(applied_);
        showFailure(_("Cannot load UniLume configuration"), error);
        return false;
    }
    editor_->setSettings(applied_);
    return true;
}

void MainWindow::apply()
{
    if (applying_) {
        return;
    }
    Settings candidate = editor_->settings();
    const ValidationResult validation = validate(candidate);
    if (!validation.ok()) {
        editor_->focusField(validation.errors.front().field);
        showFailure(_("Invalid configuration"),
                    validation.errors.front().field + ": " +
                        validation.errors.front().message);
        return;
    }

    applying_ = true;
    updateButtons();
    StageResult staged = generations_.stage(candidate);
    if (!staged.ok()) {
        applying_ = false;
        showFailure(_("Cannot stage configuration"), staged.error);
        updateButtons();
        return;
    }

    std::string error;
    if (!client_.apply(staged.settings, &error)) {
        std::string rollback_error;
        const bool rolled_back =
            !loaded_ || client_.apply(applied_, &rollback_error);
        std::string discard_error;
        const bool removed =
            generations_.discard(staged.generation, &discard_error);
        (void)removed;
        applying_ = false;
        if (!rolled_back) {
            error += "; rollback failed: " + rollback_error;
        }
        showFailure(_("Cannot apply configuration"), error);
        updateButtons();
        return;
    }

    applied_ = std::move(staged.settings);
    loaded_ = true;
    editor_->setSettings(applied_);
    generations_.collect(staged.generation);
    applying_ = false;
    statusBar()->showMessage(_("Configuration applied."), 4000);
    updateButtons();
}

void MainWindow::cancel()
{
    if (confirmDiscard()) {
        editor_->setSettings(applied_);
        close();
    }
}

void MainWindow::reset()
{
    editor_->setSettings(defaultSettings());
    updateButtons();
}

void MainWindow::importBackup()
{
    const QString path = QFileDialog::getOpenFileName(
        this, _("Import UniLume backup"), {},
        _("UniLume backups (*.ulbackup);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) ||
        file.size() > static_cast<qint64>(max_backup_bytes)) {
        showFailure(_("Cannot import backup"),
                    "backup cannot be read or exceeds the size limit");
        return;
    }
    const QByteArray data = file.readAll();
    const BackupDecodeResult decoded =
        decodeBackup(std::string_view(data.constData(), data.size()));
    if (!decoded.ok()) {
        showFailure(_("Cannot import backup"), decoded.error);
        return;
    }
    const QString summary = backupSummary(decoded.settings,
                                          decoded.migrated);
    if (QMessageBox::question(
            this, _("Preview imported backup"),
            summary + "\n\n" +
                _("Stage these values? Runtime configuration will not "
                  "change until Apply is pressed.")) != QMessageBox::Yes) {
        return;
    }
    editor_->setSettings(decoded.settings);
    editor_->setBackupPreview(summary);
    updateButtons();
}

void MainWindow::exportBackup()
{
    const Settings candidate = editor_->settings();
    const std::string backup = encodeBackup(candidate);
    if (backup.empty()) {
        showFailure(_("Cannot export backup"),
                    "current staged configuration is invalid");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, _("Export UniLume backup"), "unilume.ulbackup",
        _("UniLume backups (*.ulbackup)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        showFailure(_("Cannot export backup"),
                    file.errorString().toStdString());
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner |
                        QFileDevice::WriteOwner);
    if (file.write(backup.data(),
                   static_cast<qint64>(backup.size())) !=
            static_cast<qint64>(backup.size()) ||
        !file.commit()) {
        showFailure(_("Cannot export backup"),
                    file.errorString().toStdString());
        return;
    }
    editor_->setBackupPreview(backupSummary(candidate, false));
}

void MainWindow::updateButtons()
{
    const bool dirty = editor_->settings() != applied_;
    apply_button_->setEnabled(!applying_ && dirty && editor_->valid());
    reset_button_->setEnabled(!applying_ &&
                              editor_->settings() != defaultSettings());
    cancel_button_->setEnabled(!applying_);
    setWindowModified(dirty);
}

bool MainWindow::confirmDiscard()
{
    if (editor_->settings() == applied_) {
        return true;
    }
    return QMessageBox::question(
               this, _("Discard unapplied changes?"),
               _("Cancel closes the window without changing the running "
                 "or persisted Fcitx configuration."),
               QMessageBox::Discard | QMessageBox::Cancel,
               QMessageBox::Cancel) == QMessageBox::Discard;
}

void MainWindow::showFailure(const QString &title,
                             const std::string &error)
{
    QMessageBox::critical(this, title,
                          QString::fromStdString(error));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (applying_ || !confirmDiscard()) {
        event->ignore();
        return;
    }
    event->accept();
}

} // namespace unilume::config_gui
