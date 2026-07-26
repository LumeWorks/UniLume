// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "input_context_state.h"
#include "input_method_config.h"
#include "macro_contract.h"
#include "keymap_contract.h"

#include <fcitx/addonfactory.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <map>
#include <cstdint>
#include <string>

namespace unilume::fcitx5 {

class UniLumeAddon final : public fcitx::InputMethodEngine {
public:
    explicit UniLumeAddon(fcitx::Instance &instance);

    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;
    const fcitx::Configuration *getConfigForInputMethod(
        const fcitx::InputMethodEntry &entry) const override;
    void setConfigForInputMethod(const fcitx::InputMethodEntry &entry,
                                 const fcitx::RawConfig &config) override;

private:
    struct MacroRuntime {
        config::Snapshot configuration{config::defaults()};
        macro::Snapshot snapshot;
        std::uint64_t generation{};
        keymap::Snapshot keymap_snapshot;
        std::uint64_t keymap_generation{};
    };

    InputMethodConfig &configFor(const fcitx::InputMethodEntry &entry) const;
    MacroRuntime &macroFor(const fcitx::InputMethodEntry &entry) const;
    bool prepareMacroUpdate(const fcitx::InputMethodEntry &entry,
                            const fcitx::RawConfig &source,
                            MacroRuntime &runtime) const;
    bool prepareKeymapUpdate(const fcitx::InputMethodEntry &entry,
                             const fcitx::RawConfig &source,
                             MacroRuntime &runtime) const;

    fcitx::Instance &instance_;
    fcitx::FactoryFor<InputContextState> state_factory_;
    mutable std::map<std::string, InputMethodConfig> input_method_configs_;
    mutable std::map<std::string, MacroRuntime> macro_runtimes_;
};

class UniLumeFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace unilume::fcitx5
