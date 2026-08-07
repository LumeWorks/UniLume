// SPDX-License-Identifier: GPL-2.0-or-later

#include "addon.h"

#include "dictionary_store.h"
#include "engine_options.h"
#include "macro_store.h"
#include "keymap_contract.h"

#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>

#include <filesystem>
#include <fstream>
#include <set>

#include <filesystem>
#include <fstream>
#include <set>

namespace unilume::fcitx5 {

class UniLumeAddon::ConfigAction final : public fcitx::Action {
public:
    ConfigAction(UniLumeAddon &addon, StatusCommand command)
        : addon_(addon), command_(command)
    {
        setCheckable(statusIsCheckable(command_));
    }

    std::string shortText(fcitx::InputContext *) const override
    {
        return _(statusShortText(command_));
    }

    std::string icon(fcitx::InputContext *input_context) const override
    {
        return addon_.statusIcon(input_context);
    }

    bool isChecked(fcitx::InputContext *input_context) const override
    {
        return statusIsChecked(
            command_, addon_.statusSnapshotFor(input_context));
    }

    std::string longText(fcitx::InputContext *input_context) const override
    {
        const StatusSnapshot snapshot =
            addon_.statusSnapshotFor(input_context);
        return _(statusLongText(command_, snapshot));
    }

    void activate(fcitx::InputContext *input_context) override
    {
        addon_.applyStatusCommand(input_context, command_);
    }

private:
    UniLumeAddon &addon_;
    StatusCommand command_;
};

class UniLumeAddon::EmojiAction final : public fcitx::Action {
public:
    EmojiAction(UniLumeAddon &addon, bool clear_history)
        : addon_(addon), clear_history_(clear_history)
    {
    }

    std::string shortText(fcitx::InputContext *) const override
    {
        return clear_history_ ? _("Clear emoji history")
                              : _("Emoji picker");
    }

    std::string icon(fcitx::InputContext *) const override
    {
        return clear_history_ ? "edit-clear-history" : "face-smile";
    }

    std::string longText(fcitx::InputContext *) const override
    {
        return clear_history_
                   ? _("Remove the bounded local recently used emoji list")
                   : _("Search Fcitx emoji data without network access");
    }

    void activate(fcitx::InputContext *input_context) override
    {
        if (clear_history_) {
            addon_.clearEmojiHistory(input_context);
        } else {
            addon_.openEmojiPicker(input_context);
        }
    }

private:
    UniLumeAddon &addon_;
    bool clear_history_{};
};

class UniLumeAddon::ModeAction final : public fcitx::Action {
public:
    ModeAction(UniLumeAddon &addon,
               std::optional<policy::ApplicationMode> mode)
        : addon_(addon), mode_(mode)
    {
        setCheckable(mode_.has_value());
    }

    std::string shortText(fcitx::InputContext *input_context) const override
    {
        if (mode_) {
            switch (*mode_) {
            case policy::ApplicationMode::automatic:
                return _("Automatic");
            case policy::ApplicationMode::direct:
                return _("Direct");
            case policy::ApplicationMode::safe_preedit:
                return _("Safe preedit");
            case policy::ApplicationMode::off:
                return _("Off");
            }
        }
        const InputContextState *state = addon_.stateFor(input_context);
        if (!state) {
            return _("UniLume mode");
        }
        switch (state->requestedApplicationMode()) {
        case policy::ApplicationMode::automatic:
            return state->effectiveInputPath() == platform::InputPath::direct
                       ? _("Automatic - Atomic direct")
                       : _("Automatic - Atomic replacement unavailable");
        case policy::ApplicationMode::direct:
            if (state->effectiveInputPath() != platform::InputPath::direct) {
                return _("Direct unavailable - Passthrough");
            }
            return state->directStrategy() == DirectStrategy::fast
                       ? _("Direct - Fast")
                       : _("Direct - Guarded");
        case policy::ApplicationMode::safe_preedit:
            return _("Safe preedit");
        case policy::ApplicationMode::off:
            return _("Off");
        }
        return _("UniLume mode");
    }

    std::string icon(fcitx::InputContext *input_context) const override
    {
        return addon_.statusIcon(input_context);
    }

    bool isChecked(fcitx::InputContext *input_context) const override
    {
        const InputContextState *state = addon_.stateFor(input_context);
        return mode_ && state &&
               state->requestedApplicationMode() == *mode_;
    }

    std::string longText(fcitx::InputContext *input_context) const override
    {
        if (mode_) {
            return _("Select this mode for the current input context");
        }
        const InputContextState *state = addon_.stateFor(input_context);
        if (!state) {
            return _("UniLume application input mode");
        }
        std::string result;
        if (state->hasApplicationModeOverride()) {
            result = _("Selected for this input context");
        } else {
            switch (state->applicationPolicySource()) {
            case policy::ResolutionSource::missing_identity:
                result = _("Using direct mode without an application identity");
                break;
            case policy::ResolutionSource::exact_rule:
                result = _("Matched exact application rule: ");
                result += state->applicationPolicyPattern();
                break;
            case policy::ResolutionSource::prefix_rule:
                result = _("Matched application rule prefix: ");
                result += state->applicationPolicyPattern();
                result += '*';
                break;
            case policy::ResolutionSource::default_rule:
                result = _("Using the default application policy");
                break;
            }
        }
        if (state->requestedApplicationMode() ==
                policy::ApplicationMode::direct &&
            state->effectiveInputPath() == platform::InputPath::off) {
            result += _("; direct replacement unavailable, passing keys through");
        }
        return result;
    }

    void activate(fcitx::InputContext *input_context) override
    {
        addon_.selectModeFromAction(input_context, mode_);
    }

private:
    UniLumeAddon &addon_;
    std::optional<policy::ApplicationMode> mode_;
};

UniLumeAddon::~UniLumeAddon() = default;

UniLumeAddon::UniLumeAddon(fcitx::Instance &instance)
    : instance_(instance),
      state_factory_([this](fcitx::InputContext &input_context) {
          return new InputContextState(input_context, uinput_device_);
      })
{
    fcitx::registerDomain("unilume", UNILUME_LOCALE_DIR);
    emoji_picker_ = std::make_unique<EmojiPicker>(
        instance_,
        std::filesystem::path(
            fcitx::StandardPath::global().userDirectory(
                fcitx::StandardPath::Type::PkgData)) /
            "unilume" / "emoji-history-v1");
    instance_.inputContextManager().registerProperty(
        "unilume-input-context", &state_factory_);
    mode_menu_ = std::make_unique<fcitx::Menu>();
    mode_action_ = std::make_unique<ModeAction>(*this, std::nullopt);
    automatic_mode_action_ = std::make_unique<ModeAction>(
        *this, policy::ApplicationMode::automatic);
    direct_mode_action_ = std::make_unique<ModeAction>(
        *this, policy::ApplicationMode::direct);
    safe_preedit_mode_action_ = std::make_unique<ModeAction>(
        *this, policy::ApplicationMode::safe_preedit);
    off_mode_action_ = std::make_unique<ModeAction>(
        *this, policy::ApplicationMode::off);
    telex_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::select_telex);
    vni_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::select_vni);
    viqr_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::select_viqr);
    utf8_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::show_utf8);
    spell_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::toggle_spell_check);
    macro_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::toggle_macros);
    dictionary_action_ = std::make_unique<ConfigAction>(
        *this, StatusCommand::toggle_dictionary);
    emoji_action_ = std::make_unique<EmojiAction>(*this, false);
    clear_emoji_history_action_ =
        std::make_unique<EmojiAction>(*this, true);
    mode_action_->registerAction(
        "unilume-mode", &instance_.userInterfaceManager());
    automatic_mode_action_->registerAction(
        "unilume-mode-automatic", &instance_.userInterfaceManager());
    direct_mode_action_->registerAction(
        "unilume-mode-direct", &instance_.userInterfaceManager());
    safe_preedit_mode_action_->registerAction(
        "unilume-mode-safe-preedit", &instance_.userInterfaceManager());
    off_mode_action_->registerAction(
        "unilume-mode-off", &instance_.userInterfaceManager());
    telex_action_->registerAction(
        "unilume-input-method-telex", &instance_.userInterfaceManager());
    vni_action_->registerAction(
        "unilume-input-method-vni", &instance_.userInterfaceManager());
    viqr_action_->registerAction(
        "unilume-input-method-viqr", &instance_.userInterfaceManager());
    utf8_action_->registerAction(
        "unilume-output-utf8", &instance_.userInterfaceManager());
    spell_action_->registerAction(
        "unilume-spell-check", &instance_.userInterfaceManager());
    macro_action_->registerAction(
        "unilume-macros", &instance_.userInterfaceManager());
    dictionary_action_->registerAction(
        "unilume-dictionary", &instance_.userInterfaceManager());
    emoji_action_->registerAction(
        "unilume-emoji-picker", &instance_.userInterfaceManager());
    clear_emoji_history_action_->registerAction(
        "unilume-clear-emoji-history",
        &instance_.userInterfaceManager());
    mode_menu_->addAction(automatic_mode_action_.get());
    mode_menu_->addAction(direct_mode_action_.get());
    mode_menu_->addAction(safe_preedit_mode_action_.get());
    mode_menu_->addAction(off_mode_action_.get());
    mode_menu_->addAction(telex_action_.get());
    mode_menu_->addAction(vni_action_.get());
    mode_menu_->addAction(viqr_action_.get());
    mode_menu_->addAction(utf8_action_.get());
    mode_menu_->addAction(spell_action_.get());
    mode_menu_->addAction(macro_action_.get());
    mode_menu_->addAction(dictionary_action_.get());
    mode_menu_->addAction(emoji_action_.get());
    mode_menu_->addAction(clear_emoji_history_action_.get());
    mode_action_->setMenu(mode_menu_.get());
}

std::string UniLumeAddon::subMode(
    const fcitx::InputMethodEntry &,
    fcitx::InputContext &input_context)
{
    return mode_action_->shortText(&input_context);
}

std::string UniLumeAddon::subModeIconImpl(
    const fcitx::InputMethodEntry &,
    fcitx::InputContext &input_context)
{
    return statusIcon(&input_context);
}

std::string UniLumeAddon::subModeLabelImpl(
    const fcitx::InputMethodEntry &,
    fcitx::InputContext &input_context)
{
    const InputContextState *state = stateFor(&input_context);
    if (state &&
        state->requestedApplicationMode() ==
            policy::ApplicationMode::off) {
        return _("Off");
    }
    return state && state->effectiveInputPath() == platform::InputPath::off
               ? _("Off")
               : "VI";
}

void UniLumeAddon::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event)
{
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    synchronizeState(entry, *event.inputContext(), *state);
    event.inputContext()->statusArea().addAction(
        fcitx::StatusGroup::InputMethod, mode_action_.get());
    updateModeActions(event.inputContext());
}

void UniLumeAddon::deactivate(const fcitx::InputMethodEntry &,
                              fcitx::InputContextEvent &event)
{
    emoji_picker_->reset(event.inputContext());
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    state->focusReset();
}

void UniLumeAddon::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &event)
{
    if (emoji_picker_->active(event.inputContext())) {
        if (emoji_picker_->handle(event)) {
            event.filterAndAccept();
        }
        return;
    }
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    const RuntimeResources &resources = resourcesFor(entry);
    const std::uint64_t previous_revision =
        state->applicationModeRevision();
    const platform::InputPath previous_path =
        state->effectiveInputPath();
    synchronizeState(entry, *event.inputContext(), *state);
    if (resources.emoji_enabled && !event.isRelease() &&
        resources.mode_hotkeys.emoji.isValid() &&
        event.key().check(resources.mode_hotkeys.emoji)) {
        openEmojiPicker(event.inputContext());
        if (emoji_picker_->active(event.inputContext())) {
            event.filterAndAccept();
            return;
        }
    }
    if (handleModeHotkey(resources.mode_hotkeys, event, *state)) {
        return;
    }
    state->keyEvent(event);
    if (state->applicationModeRevision() != previous_revision ||
        state->effectiveInputPath() != previous_path) {
        updateModeActions(event.inputContext());
    }
}

void UniLumeAddon::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event)
{
    emoji_picker_->reset(event.inputContext());
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    synchronizeState(entry, *event.inputContext(), *state);
    state->reset();
}

const fcitx::Configuration *UniLumeAddon::getConfigForInputMethod(
    const fcitx::InputMethodEntry &entry) const
{
    return &configFor(entry);
}

void UniLumeAddon::setConfigForInputMethod(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &config)
{
    RuntimeResources prepared = resourcesFor(entry);
    if (!validateInputMethodConfig(config) ||
        !prepareMacroUpdate(entry, config, prepared) ||
        !prepareKeymapUpdate(entry, config, prepared) ||
        !prepareDictionaryUpdate(entry, config, prepared) ||
        !prepareApplicationPolicyUpdate(entry, config, prepared) ||
        !prepareModeHotkeyUpdate(entry, config, prepared) ||
        !loadInputMethodConfig(configFor(entry), config)) {
        return;
    }
    prepared.configuration = snapshotFromConfig(configFor(entry));
    prepared.typing_options =
        typingOptionsFromConfig(configFor(entry));
    prepared.verified_direct_enabled =
        *configFor(entry).verified_direct_enabled;
    prepared.direct_strategy = toDirectStrategy(
        *configFor(entry).direct_strategy);
    prepared.emoji_enabled = *configFor(entry).emoji_enabled;
    resourcesFor(entry) = std::move(prepared);
    instance_.inputContextManager().foreach(
        [this, &entry](fcitx::InputContext *input_context) {
            const fcitx::InputMethodEntry *active =
                instance_.inputMethodEntry(input_context);
            if (!active ||
                active->uniqueName() != entry.uniqueName()) {
                return true;
            }
            InputContextState *state = stateFor(input_context);
            if (state) {
                synchronizeState(entry, *input_context, *state);
                updateModeActions(input_context);
            }
            return true;
        });
}

void UniLumeAddon::synchronizeState(
    const fcitx::InputMethodEntry &entry,
    fcitx::InputContext &input_context,
    InputContextState &state) const
{
    const RuntimeResources &resources = resourcesFor(entry);
    const config::Snapshot &snapshot = resources.configuration;
    state.setInputMethod(toUlInputMethod(snapshot.input_method));
    state.setOptions(core::engineOptionsFromSnapshot(snapshot));
    state.setTypingOptions(resources.typing_options);
    state.setMacros(resources.snapshot, resources.generation);
    state.setKeymap(resources.keymap_snapshot, resources.keymap_generation);
    state.setDictionary(resources.dictionary_snapshot,
                        resources.dictionary_generation);
    state.setVerifiedDirectEnabled(resources.verified_direct_enabled);
    state.setDirectStrategy(resources.direct_strategy);
    const std::string &identity = input_context.program();
    if (!state.applicationPolicyIsCurrent(
            resources.application_policy_generation, identity)) {
        state.setApplicationPolicy(
            policy::resolve(resources.application_policy_snapshot, identity),
            resources.application_policy_generation, identity);
    }
}

bool UniLumeAddon::handleModeHotkey(const ModeHotkeys &hotkeys,
                                    fcitx::KeyEvent &event,
                                    InputContextState &state)
{
    if (event.isRelease()) {
        return false;
    }
    const fcitx::Key key = event.key();
    if (hotkeys.cycle.isValid() && key.check(hotkeys.cycle)) {
        state.cycleApplicationMode();
    } else if (hotkeys.automatic.isValid() &&
               key.check(hotkeys.automatic)) {
        state.selectApplicationMode(policy::ApplicationMode::automatic);
    } else if (hotkeys.direct.isValid() && key.check(hotkeys.direct)) {
        state.selectApplicationMode(policy::ApplicationMode::direct);
    } else if (hotkeys.safe_preedit.isValid() &&
               key.check(hotkeys.safe_preedit)) {
        state.selectApplicationMode(policy::ApplicationMode::safe_preedit);
    } else if (hotkeys.off.isValid() && key.check(hotkeys.off)) {
        state.selectApplicationMode(policy::ApplicationMode::off);
    } else {
        return false;
    }
    updateModeActions(event.inputContext());
    event.filterAndAccept();
    return true;
}

InputContextState *UniLumeAddon::stateFor(
    fcitx::InputContext *input_context) const
{
    return input_context
               ? input_context->propertyFor(&state_factory_)
               : nullptr;
}

void UniLumeAddon::selectModeFromAction(
    fcitx::InputContext *input_context,
    std::optional<policy::ApplicationMode> mode)
{
    InputContextState *state = stateFor(input_context);
    if (!state) {
        return;
    }
    if (mode) {
        state->selectApplicationMode(*mode);
    } else {
        state->cycleApplicationMode();
    }
    updateModeActions(input_context);
}

void UniLumeAddon::updateModeActions(fcitx::InputContext *input_context)
{
    mode_action_->update(input_context);
    automatic_mode_action_->update(input_context);
    direct_mode_action_->update(input_context);
    safe_preedit_mode_action_->update(input_context);
    off_mode_action_->update(input_context);
    telex_action_->update(input_context);
    vni_action_->update(input_context);
    viqr_action_->update(input_context);
    utf8_action_->update(input_context);
    spell_action_->update(input_context);
    macro_action_->update(input_context);
    dictionary_action_->update(input_context);
    emoji_action_->update(input_context);
    clear_emoji_history_action_->update(input_context);
    input_context->updateUserInterface(
        fcitx::UserInterfaceComponent::StatusArea);
}

StatusSnapshot UniLumeAddon::statusSnapshotFor(
    fcitx::InputContext *input_context) const
{
    const fcitx::InputMethodEntry *entry =
        input_context ? instance_.inputMethodEntry(input_context) : nullptr;
    if (!entry) {
        return {};
    }
    const InputMethodConfig &config = configFor(*entry);
    return {
        *config.input_method,
        *config.spell_check,
        *config.macro_enabled,
        *config.dictionary_enabled,
        !config.macro_file->empty(),
        !config.dictionary_file->empty(),
    };
}

void UniLumeAddon::applyStatusCommand(
    fcitx::InputContext *input_context,
    StatusCommand command)
{
    const fcitx::InputMethodEntry *entry =
        input_context ? instance_.inputMethodEntry(input_context) : nullptr;
    if (!entry) {
        return;
    }
    const std::optional<StatusMutation> mutation =
        statusMutation(command, statusSnapshotFor(input_context));
    if (!mutation) {
        updateModeActions(input_context);
        return;
    }
    fcitx::RawConfig update;
    update.setValueByPath(mutation->path, mutation->value);
    setConfigForInputMethod(*entry, update);
    updateModeActions(input_context);
}

void UniLumeAddon::openEmojiPicker(
    fcitx::InputContext *input_context)
{
    const fcitx::InputMethodEntry *entry =
        input_context ? instance_.inputMethodEntry(input_context) : nullptr;
    if (!entry || !*configFor(*entry).emoji_enabled ||
        !emoji_picker_->available()) {
        return;
    }
    if (InputContextState *state = stateFor(input_context)) {
        state->suspendComposition();
    }
    const bool opened = emoji_picker_->trigger(input_context);
    (void)opened;
}

void UniLumeAddon::clearEmojiHistory(
    fcitx::InputContext *input_context)
{
    const bool cleared = emoji_picker_->clearHistory();
    (void)cleared;
    if (emoji_picker_->active(input_context)) {
        emoji_picker_->reset(input_context);
        const bool reopened = emoji_picker_->trigger(input_context);
        (void)reopened;
    }
}

std::string UniLumeAddon::statusIcon(
    fcitx::InputContext *input_context) const
{
    const InputContextState *state = stateFor(input_context);
    if (state &&
        state->requestedApplicationMode() ==
            policy::ApplicationMode::off) {
        return "unilume-off";
    }
    if (state && state->effectiveInputPath() == platform::InputPath::off) {
        return "unilume-fallback";
    }
    return "unilume";
}

void UniLumeAddon::synchronizeState(
    const fcitx::InputMethodEntry &entry,
    fcitx::InputContext &input_context,
    InputContextState &state) const
{
    const RuntimeResources &resources = resourcesFor(entry);
    const config::Snapshot &snapshot = resources.configuration;
    state.setInputMethod(toUlInputMethod(snapshot.input_method));
    state.setOptions(core::engineOptionsFromSnapshot(snapshot));
    state.setMacros(resources.snapshot, resources.generation);
    state.setKeymap(resources.keymap_snapshot, resources.keymap_generation);
    state.setDictionary(resources.dictionary_snapshot,
                        resources.dictionary_generation);
    const std::string &identity = input_context.program();
    if (!state.applicationPolicyIsCurrent(
            resources.application_policy_generation, identity)) {
        state.setApplicationPolicy(
            policy::resolve(resources.application_policy_snapshot, identity),
            resources.application_policy_generation, identity);
    }
}

bool UniLumeAddon::handleModeHotkey(const ModeHotkeys &hotkeys,
                                    fcitx::KeyEvent &event,
                                    InputContextState &state)
{
    if (event.isRelease()) {
        return false;
    }
    const fcitx::Key key = event.key();
    if (hotkeys.cycle.isValid() && key.check(hotkeys.cycle)) {
        state.cycleApplicationMode();
    } else if (hotkeys.automatic.isValid() &&
               key.check(hotkeys.automatic)) {
        state.selectApplicationMode(policy::ApplicationMode::automatic);
    } else if (hotkeys.direct.isValid() && key.check(hotkeys.direct)) {
        state.selectApplicationMode(policy::ApplicationMode::direct);
    } else if (hotkeys.safe_preedit.isValid() &&
               key.check(hotkeys.safe_preedit)) {
        state.selectApplicationMode(policy::ApplicationMode::safe_preedit);
    } else if (hotkeys.off.isValid() && key.check(hotkeys.off)) {
        state.selectApplicationMode(policy::ApplicationMode::off);
    } else {
        return false;
    }
    updateModeActions(event.inputContext());
    event.filterAndAccept();
    return true;
}

InputContextState *UniLumeAddon::stateFor(
    fcitx::InputContext *input_context) const
{
    return input_context
               ? input_context->propertyFor(&state_factory_)
               : nullptr;
}

void UniLumeAddon::selectModeFromAction(
    fcitx::InputContext *input_context,
    std::optional<policy::ApplicationMode> mode)
{
    InputContextState *state = stateFor(input_context);
    if (!state) {
        return;
    }
    if (mode) {
        state->selectApplicationMode(*mode);
    } else {
        state->cycleApplicationMode();
    }
    updateModeActions(input_context);
}

void UniLumeAddon::updateModeActions(fcitx::InputContext *input_context)
{
    mode_action_->update(input_context);
    automatic_mode_action_->update(input_context);
    direct_mode_action_->update(input_context);
    safe_preedit_mode_action_->update(input_context);
    off_mode_action_->update(input_context);
    input_context->updateUserInterface(
        fcitx::UserInterfaceComponent::StatusArea);
}

bool UniLumeAddon::prepareKeymapUpdate(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &source,
    RuntimeResources &runtime) const
{
    const InputMethodConfig &current = configFor(entry);
    const std::string *enabled_value = source.valueByPath("KeymapEnabled");
    const std::string *path_value = source.valueByPath("KeymapFile");
    if (!enabled_value && !path_value) {
        return true;
    }
    const bool enabled =
        enabled_value ? *enabled_value == "True" : *current.keymap_enabled;
    const std::string path =
        path_value ? *path_value : *current.keymap_file;
    keymap::Snapshot snapshot;
    if (enabled) {
        if (path.empty()) {
            return false;
        }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > keymap::max_serialized_bytes) {
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return false;
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
            return false;
        }
        char extra = 0;
        if (stream.get(extra)) {
            return false;
        }
        keymap::DecodeResult decoded = keymap::decode(text);
        if (!decoded.ok()) {
            return false;
        }
        snapshot = std::move(decoded.snapshot);
    }
    runtime.keymap_snapshot = std::move(snapshot);
    ++runtime.keymap_generation;
    if (runtime.keymap_generation == 0) {
        ++runtime.keymap_generation;
    }
    return true;
}

bool UniLumeAddon::prepareApplicationPolicyUpdate(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &source,
    RuntimeResources &runtime) const
{
    const InputMethodConfig &current = configFor(entry);
    const std::string *enabled_value =
        source.valueByPath("ApplicationPolicyEnabled");
    const std::string *path_value =
        source.valueByPath("ApplicationPolicyFile");
    if (!enabled_value && !path_value) {
        return true;
    }
    const bool enabled =
        enabled_value ? *enabled_value == "True"
                      : *current.application_policy_enabled;
    const std::string path =
        path_value ? *path_value : *current.application_policy_file;
    policy::Snapshot snapshot;
    if (enabled) {
        if (path.empty()) {
            return false;
        }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > policy::max_serialized_bytes) {
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return false;
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
            return false;
        }
        char extra = 0;
        if (stream.get(extra)) {
            return false;
        }
        policy::DecodeResult decoded = policy::decode(text);
        if (!decoded.ok()) {
            return false;
        }
        if (decoded.legacy_modes && !legacy_policy_warning_emitted_) {
            FCITX_WARN()
                << "UniLume application policy uses legacy modes; "
                   "automatic maps to direct and safe-preedit maps to off";
            legacy_policy_warning_emitted_ = true;
        }
        snapshot = std::move(decoded.snapshot);
    }
    runtime.application_policy_snapshot = std::move(snapshot);
    ++runtime.application_policy_generation;
    if (runtime.application_policy_generation == 0) {
        ++runtime.application_policy_generation;
    }
    return true;
}

bool UniLumeAddon::prepareModeHotkeyUpdate(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &source,
    RuntimeResources &runtime) const
{
    const InputMethodConfig &current = configFor(entry);
    const auto effective = [&source](const char *name,
                                     const std::string &active) {
        const std::string *value = source.valueByPath(name);
        return value ? *value : active;
    };
    const bool has_update =
        source.valueByPath("CycleModeHotkey") ||
        source.valueByPath("AutomaticModeHotkey") ||
        source.valueByPath("DirectModeHotkey") ||
        source.valueByPath("SafePreeditModeHotkey") ||
        source.valueByPath("OffModeHotkey") ||
        source.valueByPath("EmojiHotkey");
    if (!has_update) {
        return true;
    }
    const std::string cycle = effective(
        "CycleModeHotkey", *current.cycle_mode_hotkey);
    const std::string automatic = effective(
        "AutomaticModeHotkey", *current.automatic_mode_hotkey);
    const std::string direct = effective(
        "DirectModeHotkey", *current.direct_mode_hotkey);
    const std::string safe_preedit = effective(
        "SafePreeditModeHotkey", *current.safe_preedit_mode_hotkey);
    const std::string off = effective(
        "OffModeHotkey", *current.off_mode_hotkey);
    const std::string emoji = effective(
        "EmojiHotkey", *current.emoji_hotkey);
    ModeHotkeys parsed{
        cycle.empty() ? fcitx::Key() : fcitx::Key(cycle),
        automatic.empty() ? fcitx::Key() : fcitx::Key(automatic),
        direct.empty() ? fcitx::Key() : fcitx::Key(direct),
        safe_preedit.empty() ? fcitx::Key() : fcitx::Key(safe_preedit),
        off.empty() ? fcitx::Key() : fcitx::Key(off),
        emoji.empty() ? fcitx::Key() : fcitx::Key(emoji),
    };
    std::set<std::string> seen;
    for (const fcitx::Key *key :
         {&parsed.cycle, &parsed.automatic, &parsed.direct,
          &parsed.safe_preedit, &parsed.off, &parsed.emoji}) {
        if (!key->isValid()) {
            continue;
        }
        if (!seen.emplace(key->normalize().toString()).second) {
            return false;
        }
    }
    runtime.mode_hotkeys = std::move(parsed);
    return true;
}

UniLumeAddon::RuntimeResources &UniLumeAddon::resourcesFor(
    const fcitx::InputMethodEntry &entry) const
{
    return runtime_resources_.try_emplace(entry.uniqueName()).first->second;
}

bool UniLumeAddon::prepareMacroUpdate(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &source,
    RuntimeResources &runtime) const
{
    const InputMethodConfig &current = configFor(entry);
    const std::string *enabled_value = source.valueByPath("MacroEnabled");
    const std::string *path_value = source.valueByPath("MacroFile");
    if (!enabled_value && !path_value) {
        return true;
    }
    const bool enabled = enabled_value
                             ? *enabled_value == "True"
                             : *current.macro_enabled;
    const std::string path =
        path_value ? *path_value : *current.macro_file;

    macro::Snapshot snapshot;
    if (enabled) {
        if (path.empty()) {
            return false;
        }
        macro::Store store(path);
        const macro::LoadResult loaded = store.load();
        if (!loaded.ok() ||
            loaded.disposition == macro::LoadDisposition::missing) {
            return false;
        }
        snapshot = loaded.snapshot;
        snapshot.enabled = true;
        if (loaded.disposition == macro::LoadDisposition::migrated) {
            std::string error;
            if (!store.save(snapshot, &error)) {
                return false;
            }
        }
    }
    runtime.snapshot = std::move(snapshot);
    ++runtime.generation;
    if (runtime.generation == 0) {
        ++runtime.generation;
    }
    return true;
}

bool UniLumeAddon::prepareDictionaryUpdate(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &source,
    RuntimeResources &runtime) const
{
    const InputMethodConfig &current = configFor(entry);
    const std::string *enabled_value =
        source.valueByPath("DictionaryEnabled");
    const std::string *path_value = source.valueByPath("DictionaryFile");
    if (!enabled_value && !path_value) {
        return true;
    }
    const bool enabled = enabled_value
                             ? *enabled_value == "True"
                             : *current.dictionary_enabled;
    const std::string path =
        path_value ? *path_value : *current.dictionary_file;
    dictionary::Snapshot snapshot;
    if (enabled) {
        if (path.empty()) {
            return false;
        }
        dictionary::Store store(path);
        const dictionary::LoadResult loaded = store.load();
        if (!loaded.ok() ||
            loaded.disposition == dictionary::LoadDisposition::missing) {
            return false;
        }
        snapshot = loaded.snapshot;
        snapshot.enabled = true;
    }
    runtime.dictionary_snapshot = std::move(snapshot);
    ++runtime.dictionary_generation;
    if (runtime.dictionary_generation == 0) {
        ++runtime.dictionary_generation;
    }
    return true;
}

InputMethodConfig &UniLumeAddon::configFor(
    const fcitx::InputMethodEntry &entry) const
{
    return input_method_configs_.try_emplace(entry.uniqueName()).first->second;
}

fcitx::AddonInstance *UniLumeFactory::create(fcitx::AddonManager *manager)
{
    return new UniLumeAddon(*manager->instance());
}

} // namespace unilume::fcitx5

#ifdef FCITX_ADDON_FACTORY_V2
FCITX_ADDON_FACTORY_V2(unilume, unilume::fcitx5::UniLumeFactory)
#else
FCITX_ADDON_FACTORY(unilume::fcitx5::UniLumeFactory)
#endif
