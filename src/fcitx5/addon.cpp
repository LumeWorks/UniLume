// SPDX-License-Identifier: GPL-2.0-or-later

#include "addon.h"

#include "dictionary_store.h"
#include "engine_options.h"
#include "macro_store.h"
#include "keymap_contract.h"

#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextmanager.h>

#include <filesystem>
#include <fstream>

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
    const RuntimeResources &resources = resourcesFor(entry);
    const config::Snapshot &snapshot = resources.configuration;
    state->setInputMethod(toUlInputMethod(snapshot.input_method));
    state->setOptions(core::engineOptionsFromSnapshot(snapshot));
    state->setMacros(resources.snapshot, resources.generation);
    state->setKeymap(resources.keymap_snapshot, resources.keymap_generation);
    state->setDictionary(resources.dictionary_snapshot,
                         resources.dictionary_generation);
    state->keyEvent(event);
}

void UniLumeAddon::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event)
{
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    const RuntimeResources &resources = resourcesFor(entry);
    const config::Snapshot &snapshot = resources.configuration;
    state->setInputMethod(toUlInputMethod(snapshot.input_method));
    state->setOptions(core::engineOptionsFromSnapshot(snapshot));
    state->setMacros(resources.snapshot, resources.generation);
    state->setKeymap(resources.keymap_snapshot, resources.keymap_generation);
    state->setDictionary(resources.dictionary_snapshot,
                         resources.dictionary_generation);
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
        !loadInputMethodConfig(configFor(entry), config)) {
        return;
    }
    prepared.configuration = snapshotFromConfig(configFor(entry));
    resourcesFor(entry) = std::move(prepared);
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
