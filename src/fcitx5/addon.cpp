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

#include <filesystem>
#include <fstream>
#include <set>

namespace unilume::fcitx5 {

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
                return "Automatic";
            case policy::ApplicationMode::direct:
                return "Direct";
            case policy::ApplicationMode::safe_preedit:
                return "Safe preedit";
            case policy::ApplicationMode::off:
                return "Off";
            }
        }
        const InputContextState *state = addon_.stateFor(input_context);
        if (!state) {
            return "UniLume mode";
        }
        switch (state->requestedApplicationMode()) {
        case policy::ApplicationMode::automatic:
            return state->effectiveInputPath() == platform::InputPath::direct
                       ? "Automatic · Direct"
                       : "Automatic · Safe preedit";
        case policy::ApplicationMode::direct:
            return state->effectiveInputPath() == platform::InputPath::direct
                       ? "Direct"
                       : "Direct · Safe fallback";
        case policy::ApplicationMode::safe_preedit:
            return "Safe preedit";
        case policy::ApplicationMode::off:
            return "Off";
        }
        return "UniLume mode";
    }

    std::string icon(fcitx::InputContext *) const override
    {
        return "input-keyboard";
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
            return "Select this mode for the current input context";
        }
        const InputContextState *state = addon_.stateFor(input_context);
        if (!state) {
            return "UniLume application input mode";
        }
        std::string result = state->applicationModeReason();
        if (state->requestedApplicationMode() ==
                policy::ApplicationMode::direct &&
            state->effectiveInputPath() == platform::InputPath::preedit) {
            result += "; direct replacement unavailable, using safe preedit";
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
      state_factory_([](fcitx::InputContext &input_context) {
          return new InputContextState(input_context);
      })
{
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
    mode_menu_->addAction(automatic_mode_action_.get());
    mode_menu_->addAction(direct_mode_action_.get());
    mode_menu_->addAction(safe_preedit_mode_action_.get());
    mode_menu_->addAction(off_mode_action_.get());
    mode_action_->setMenu(mode_menu_.get());
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

void UniLumeAddon::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &event)
{
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    const RuntimeResources &resources = resourcesFor(entry);
    const std::uint64_t previous_revision =
        state->applicationModeRevision();
    const platform::InputPath previous_path =
        state->effectiveInputPath();
    synchronizeState(entry, *event.inputContext(), *state);
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
    prepared.verified_direct_enabled =
        *configFor(entry).verified_direct_enabled;
    resourcesFor(entry) = std::move(prepared);
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
    state.setVerifiedDirectEnabled(resources.verified_direct_enabled);
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
        source.valueByPath("OffModeHotkey");
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
    ModeHotkeys parsed{
        cycle.empty() ? fcitx::Key() : fcitx::Key(cycle),
        automatic.empty() ? fcitx::Key() : fcitx::Key(automatic),
        direct.empty() ? fcitx::Key() : fcitx::Key(direct),
        safe_preedit.empty() ? fcitx::Key() : fcitx::Key(safe_preedit),
        off.empty() ? fcitx::Key() : fcitx::Key(off),
    };
    std::set<std::string> seen;
    for (const fcitx::Key *key :
         {&parsed.cycle, &parsed.automatic, &parsed.direct,
          &parsed.safe_preedit, &parsed.off}) {
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
