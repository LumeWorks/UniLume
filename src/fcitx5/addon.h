// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "input_context_state.h"
#include "input_method_config.h"
#include "dictionary_contract.h"
#include "macro_contract.h"
#include "keymap_contract.h"
#include "application_policy.h"

#include <fcitx-utils/key.h>
#include <fcitx/addonfactory.h>
#include <fcitx/action.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>

#include <map>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace unilume::fcitx5 {

class UniLumeAddon final : public fcitx::InputMethodEngine {
public:
    explicit UniLumeAddon(fcitx::Instance &instance);
    ~UniLumeAddon() override;

    void activate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;
    const fcitx::Configuration *getConfigForInputMethod(
        const fcitx::InputMethodEntry &entry) const override;
    void setConfigForInputMethod(const fcitx::InputMethodEntry &entry,
                                 const fcitx::RawConfig &config) override;

private:
    class ModeAction;

    struct ModeHotkeys {
        fcitx::Key cycle{"Control+Alt+u"};
        fcitx::Key automatic;
        fcitx::Key direct;
        fcitx::Key safe_preedit;
        fcitx::Key off;
    };

    struct RuntimeResources {
        config::Snapshot configuration{config::defaults()};
        macro::Snapshot snapshot;
        std::uint64_t generation{};
        keymap::Snapshot keymap_snapshot;
        std::uint64_t keymap_generation{};
        dictionary::Snapshot dictionary_snapshot;
        std::uint64_t dictionary_generation{};
        policy::Snapshot application_policy_snapshot;
        std::uint64_t application_policy_generation{};
        ModeHotkeys mode_hotkeys;
    };

    InputMethodConfig &configFor(const fcitx::InputMethodEntry &entry) const;
    RuntimeResources &resourcesFor(
        const fcitx::InputMethodEntry &entry) const;
    bool prepareMacroUpdate(const fcitx::InputMethodEntry &entry,
                            const fcitx::RawConfig &source,
                            RuntimeResources &runtime) const;
    bool prepareKeymapUpdate(const fcitx::InputMethodEntry &entry,
                             const fcitx::RawConfig &source,
                             RuntimeResources &runtime) const;
    bool prepareDictionaryUpdate(const fcitx::InputMethodEntry &entry,
                                 const fcitx::RawConfig &source,
                                 RuntimeResources &runtime) const;
    bool prepareApplicationPolicyUpdate(
        const fcitx::InputMethodEntry &entry,
        const fcitx::RawConfig &source,
        RuntimeResources &runtime) const;
    bool prepareModeHotkeyUpdate(const fcitx::InputMethodEntry &entry,
                                 const fcitx::RawConfig &source,
                                 RuntimeResources &runtime) const;
    void synchronizeState(const fcitx::InputMethodEntry &entry,
                          fcitx::InputContext &input_context,
                          InputContextState &state) const;
    bool handleModeHotkey(const ModeHotkeys &hotkeys,
                          fcitx::KeyEvent &event,
                          InputContextState &state);
    InputContextState *stateFor(fcitx::InputContext *input_context) const;
    void selectModeFromAction(
        fcitx::InputContext *input_context,
        std::optional<policy::ApplicationMode> mode);
    void updateModeActions(fcitx::InputContext *input_context);

    fcitx::Instance &instance_;
    fcitx::FactoryFor<InputContextState> state_factory_;
    std::unique_ptr<fcitx::Menu> mode_menu_;
    std::unique_ptr<ModeAction> mode_action_;
    std::unique_ptr<ModeAction> automatic_mode_action_;
    std::unique_ptr<ModeAction> direct_mode_action_;
    std::unique_ptr<ModeAction> safe_preedit_mode_action_;
    std::unique_ptr<ModeAction> off_mode_action_;
    mutable std::map<std::string, InputMethodConfig> input_method_configs_;
    mutable std::map<std::string, RuntimeResources> runtime_resources_;
};

class UniLumeFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace unilume::fcitx5
