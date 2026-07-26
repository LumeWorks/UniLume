// SPDX-License-Identifier: GPL-2.0-or-later

#include "settings_editor.h"

#include <fcitx-utils/i18n.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace unilume::config_gui {
namespace {

QString localize(std::string_view text)
{
    return QString::fromUtf8(_(std::string(text).c_str()));
}

} // namespace

SettingsEditor::SettingsEditor(QWidget *parent)
    : QWidget(parent),
      tabs_(new QTabWidget(this)),
      validation_label_(new QLabel(this))
{
    setAccessibleName(_("UniLume configuration"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs_);
    validation_label_->setWordWrap(true);
    validation_label_->setAccessibleName(_("Validation status"));
    layout->addWidget(validation_label_);

    tabs_->addTab(createScalarPage(Category::general),
                  _("General"));
    tabs_->addTab(createScalarPage(Category::typing),
                  _("Typing"));
    tabs_->addTab(
        createResourcePage(
            Category::applications, "ApplicationPolicyEnabled",
            _("Application rules editor"),
            _("One default and optional exact or trailing-* rules. "
              "Validation uses the production application-policy parser."),
            &application_editor_),
        _("Applications"));
    tabs_->addTab(
        createResourcePage(
            Category::macros, "MacroEnabled", _("Macro editor"),
            _("Canonical rows contain a key, a tab, and replacement text. "
              "Legacy imports are previewed and canonicalized on Apply."),
            &macro_editor_),
        _("Macros"));
    tabs_->addTab(
        createResourcePage(
            Category::dictionary, "DictionaryEnabled",
            _("Personal dictionary editor"),
            _("Use keep or restore, a tab, and one word per row. "
              "Line and field errors come from the production parser."),
            &dictionary_editor_),
        _("Dictionary"));
    tabs_->addTab(
        createResourcePage(
            Category::keymap, "KeymapEnabled",
            _("Custom keymap editor"),
            _("Use one printable key, '=', and a supported UniKey action. "
              "The historical runtime loader is never called."),
            &keymap_editor_),
        _("Keymap"));
    tabs_->addTab(createScalarPage(Category::shortcuts),
                  _("Shortcuts"));
    tabs_->addTab(createAppearancePage(), _("Appearance"));
    tabs_->addTab(createBackupPage(), _("Backup"));

    setSettings(defaultSettings());
}

QWidget *SettingsEditor::createScalarPage(Category category)
{
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const FieldDescriptor &descriptor : fieldDescriptors()) {
        if (descriptor.category != category ||
            descriptor.kind == FieldKind::managed_path ||
            (descriptor.key == "EmojiEnabled")) {
            continue;
        }
        QWidget *control = nullptr;
        if (descriptor.kind == FieldKind::boolean) {
            auto *checkbox = new QCheckBox(localize(descriptor.label));
            control = checkbox;
            form->addRow(checkbox);
            connect(checkbox, &QCheckBox::toggled, this,
                    &SettingsEditor::handleChanged);
        } else if (descriptor.kind == FieldKind::choice) {
            auto *combo = new QComboBox;
            for (const std::string_view choice : descriptor.choices) {
                combo->addItem(QString::fromUtf8(choice));
            }
            auto *label = new QLabel(localize(descriptor.label));
            label->setBuddy(combo);
            form->addRow(label, combo);
            control = combo;
            connect(combo, &QComboBox::currentIndexChanged, this,
                    &SettingsEditor::handleChanged);
        } else {
            auto *edit = new QLineEdit;
            edit->setClearButtonEnabled(true);
            auto *label = new QLabel(localize(descriptor.label));
            label->setBuddy(edit);
            form->addRow(label, edit);
            control = edit;
            connect(edit, &QLineEdit::textChanged, this,
                    &SettingsEditor::handleChanged);
        }
        control->setObjectName(QString::fromUtf8(descriptor.key));
        control->setAccessibleName(localize(descriptor.label));
        controls_.emplace(descriptor.key, control);
    }

    if (category == Category::shortcuts) {
        const FieldDescriptor *emoji = nullptr;
        for (const FieldDescriptor &descriptor : fieldDescriptors()) {
            if (descriptor.key == "EmojiEnabled") {
                emoji = &descriptor;
                break;
            }
        }
        if (emoji) {
            auto *checkbox = new QCheckBox(localize(emoji->label));
            checkbox->setObjectName("EmojiEnabled");
            checkbox->setAccessibleName(localize(emoji->label));
            form->addRow(checkbox);
            controls_.emplace("EmojiEnabled", checkbox);
            connect(checkbox, &QCheckBox::toggled, this,
                    &SettingsEditor::handleChanged);
        }
    }

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QWidget *SettingsEditor::createResourcePage(
    Category category,
    const char *enabled_key,
    const QString &accessible_name,
    const QString &help,
    QPlainTextEdit **editor)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    const auto descriptor = std::find_if(
        fieldDescriptors().begin(), fieldDescriptors().end(),
        [enabled_key](const FieldDescriptor &item) {
            return item.key == enabled_key;
        });
    auto *enabled_box = new QCheckBox(
        descriptor == fieldDescriptors().end()
            ? QString::fromUtf8(enabled_key)
            : localize(descriptor->label));
    enabled_box->setObjectName(enabled_key);
    enabled_box->setAccessibleName(enabled_box->text());
    controls_.emplace(enabled_key, enabled_box);
    layout->addWidget(enabled_box);
    connect(enabled_box, &QCheckBox::toggled, this,
            &SettingsEditor::handleChanged);

    auto *help_label = new QLabel(help);
    help_label->setWordWrap(true);
    layout->addWidget(help_label);
    *editor = new QPlainTextEdit;
    (*editor)->setAccessibleName(accessible_name);
    (*editor)->setLineWrapMode(QPlainTextEdit::NoWrap);
    (*editor)->setTabChangesFocus(false);
    layout->addWidget(*editor, 1);
    connect(*editor, &QPlainTextEdit::textChanged, this,
            &SettingsEditor::handleChanged);
    (void)category;
    return page;
}

QWidget *SettingsEditor::createAppearancePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *title = new QLabel(
        _("UniLume follows the desktop palette, font scale and device "
          "pixel ratio."));
    title->setWordWrap(true);
    layout->addWidget(title);
    for (const auto &[icon, text] :
         {std::pair{"unilume", _("Vietnamese processing")},
          std::pair{"unilume-off", _("Processing off")},
          std::pair{"unilume-fallback", _("Safe preedit fallback")}}) {
        auto *row = new QHBoxLayout;
        auto *preview = new QLabel;
        preview->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        preview->setAccessibleName(text);
        row->addWidget(preview);
        row->addWidget(new QLabel(text), 1);
        layout->addLayout(row);
    }
    auto *reason = new QLabel(
        _("There are no private color or scale settings: system-managed "
          "appearance avoids stale theme state and preserves accessibility."));
    reason->setWordWrap(true);
    layout->addWidget(reason);
    layout->addStretch();
    return page;
}

QWidget *SettingsEditor::createBackupPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *description = new QLabel(
        _("Export or import one versioned backup containing every production "
          "option and the four managed resource documents. Import only stages "
          "a preview; Apply performs the atomic switch."));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *buttons = new QHBoxLayout;
    auto *import_button = new QPushButton(_("Import..."));
    auto *export_button = new QPushButton(_("Export..."));
    import_button->setAccessibleName(_("Import UniLume backup"));
    export_button->setAccessibleName(_("Export UniLume backup"));
    buttons->addWidget(import_button);
    buttons->addWidget(export_button);
    buttons->addStretch();
    layout->addLayout(buttons);
    backup_preview_ = new QLabel(_("No backup has been previewed."));
    backup_preview_->setWordWrap(true);
    backup_preview_->setAccessibleName(_("Backup preview"));
    layout->addWidget(backup_preview_);
    layout->addStretch();
    connect(import_button, &QPushButton::clicked, this,
            &SettingsEditor::importRequested);
    connect(export_button, &QPushButton::clicked, this,
            &SettingsEditor::exportRequested);
    return page;
}

void SettingsEditor::setSettings(const Settings &settings)
{
    loading_ = true;
    baseline_ = settings;
    for (const FieldDescriptor &descriptor : fieldDescriptors()) {
        const auto item = controls_.find(std::string(descriptor.key));
        if (item == controls_.end()) {
            continue;
        }
        const QString current =
            QString::fromStdString(value(settings, descriptor.key));
        if (auto *checkbox = qobject_cast<QCheckBox *>(item->second)) {
            checkbox->setChecked(current == "True");
        } else if (auto *combo =
                       qobject_cast<QComboBox *>(item->second)) {
            combo->setCurrentText(current);
        } else if (auto *edit =
                       qobject_cast<QLineEdit *>(item->second)) {
            edit->setText(current);
        }
    }
    macro_editor_->setPlainText(
        QString::fromStdString(settings.resources.macros));
    dictionary_editor_->setPlainText(
        QString::fromStdString(settings.resources.dictionary));
    keymap_editor_->setPlainText(
        QString::fromStdString(settings.resources.keymap));
    application_editor_->setPlainText(
        QString::fromStdString(settings.resources.application_policy));
    loading_ = false;
    updateValidation();
}

Settings SettingsEditor::settings() const
{
    Settings current = baseline_;
    for (const FieldDescriptor &descriptor : fieldDescriptors()) {
        const auto item = controls_.find(std::string(descriptor.key));
        if (item == controls_.end()) {
            continue;
        }
        if (const auto *checkbox =
                qobject_cast<QCheckBox *>(item->second)) {
            current.values[std::string(descriptor.key)] =
                checkbox->isChecked() ? "True" : "False";
        } else if (const auto *combo =
                       qobject_cast<QComboBox *>(item->second)) {
            current.values[std::string(descriptor.key)] =
                combo->currentText().toStdString();
        } else if (const auto *edit =
                       qobject_cast<QLineEdit *>(item->second)) {
            current.values[std::string(descriptor.key)] =
                edit->text().toStdString();
        }
    }
    current.resources.macros =
        macro_editor_->toPlainText().toStdString();
    current.resources.dictionary =
        dictionary_editor_->toPlainText().toStdString();
    current.resources.keymap =
        keymap_editor_->toPlainText().toStdString();
    current.resources.application_policy =
        application_editor_->toPlainText().toStdString();
    return current;
}

bool SettingsEditor::valid() const
{
    return valid_;
}

void SettingsEditor::focusField(const std::string &field)
{
    if (field == "MacroFile") {
        macro_editor_->setFocus();
    } else if (field == "DictionaryFile") {
        dictionary_editor_->setFocus();
    } else if (field == "KeymapFile") {
        keymap_editor_->setFocus();
    } else if (field == "ApplicationPolicyFile") {
        application_editor_->setFocus();
    } else if (const auto item = controls_.find(field);
               item != controls_.end()) {
        item->second->setFocus();
    }
}

void SettingsEditor::setBackupPreview(const QString &preview)
{
    backup_preview_->setText(preview);
}

void SettingsEditor::handleChanged()
{
    if (loading_) {
        return;
    }
    updateValidation();
    emit changed();
}

void SettingsEditor::updateValidation()
{
    const ValidationResult result = validate(settings());
    const bool now_valid = result.ok();
    if (now_valid) {
        validation_label_->setText(_("All fields are valid."));
        validation_label_->setStyleSheet({});
    } else {
        validation_label_->setText(
            QString::fromStdString(result.errors.front().field + ": " +
                                   result.errors.front().message));
        validation_label_->setStyleSheet("color: palette(link);");
    }
    if (valid_ != now_valid) {
        valid_ = now_valid;
        emit validityChanged(valid_);
    }
}

} // namespace unilume::config_gui
