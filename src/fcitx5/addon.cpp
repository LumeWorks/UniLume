// SPDX-License-Identifier: GPL-2.0-or-later

#include "addon.h"

#include "engine_options.h"
#include "macro_store.h"

#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextmanager.h>

namespace unilume::fcitx5 {

UniLumeAddon::UniLumeAddon(fcitx::Instance &instance)
    : instance_(instance),
      state_factory_([](fcitx::InputContext &input_context) {
          return new InputContextState(input_context);
      })
{
    instance_.inputContextManager().registerProperty(
        "unilume-input-context", &state_factory_);
}

void UniLumeAddon::keyEvent(const fcitx::InputMethodEntry &entry,
                            fcitx::KeyEvent &event)
{
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    const MacroRuntime &macros = macroFor(entry);
    const config::Snapshot &snapshot = macros.configuration;
    state->setInputMethod(toUlInputMethod(snapshot.input_method));
    state->setOptions(core::engineOptionsFromSnapshot(snapshot));
    state->setMacros(macros.snapshot, macros.generation);
    state->keyEvent(event);
}

void UniLumeAddon::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event)
{
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    const MacroRuntime &macros = macroFor(entry);
    const config::Snapshot &snapshot = macros.configuration;
    state->setInputMethod(toUlInputMethod(snapshot.input_method));
    state->setOptions(core::engineOptionsFromSnapshot(snapshot));
    state->setMacros(macros.snapshot, macros.generation);
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
    MacroRuntime prepared = macroFor(entry);
    if (!validateInputMethodConfig(config) ||
        !prepareMacroUpdate(entry, config, prepared) ||
        !loadInputMethodConfig(configFor(entry), config)) {
        return;
    }
    prepared.configuration = snapshotFromConfig(configFor(entry));
    macroFor(entry) = std::move(prepared);
}

UniLumeAddon::MacroRuntime &UniLumeAddon::macroFor(
    const fcitx::InputMethodEntry &entry) const
{
    return macro_runtimes_.try_emplace(entry.uniqueName()).first->second;
}

bool UniLumeAddon::prepareMacroUpdate(
    const fcitx::InputMethodEntry &entry,
    const fcitx::RawConfig &source,
    MacroRuntime &runtime) const
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
