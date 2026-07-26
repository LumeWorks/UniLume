// SPDX-License-Identifier: GPL-2.0-or-later

#include "addon.h"

#include "engine_options.h"

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
    const config::Snapshot snapshot = snapshotFromConfig(configFor(entry));
    state->setInputMethod(toUlInputMethod(*configFor(entry).input_method));
    state->setOptions(core::engineOptionsFromSnapshot(snapshot));
    state->keyEvent(event);
}

void UniLumeAddon::reset(const fcitx::InputMethodEntry &entry,
                         fcitx::InputContextEvent &event)
{
    auto *state = event.inputContext()->propertyFor(&state_factory_);
    const config::Snapshot snapshot = snapshotFromConfig(configFor(entry));
    state->setInputMethod(toUlInputMethod(*configFor(entry).input_method));
    state->setOptions(core::engineOptionsFromSnapshot(snapshot));
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
    (void)loadInputMethodConfig(configFor(entry), config);
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
