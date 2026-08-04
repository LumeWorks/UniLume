// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "input_context_state.h"
#include "input_method_config.h"
#include "emoji_picker.h"
#include "status_action_model.h"
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

class UniLumeAddon final : public fcitx::InputMethodEngineV2 {
public:
    explicit UniLumeAddon(fcitx::Instance &instance);
    ~UniLumeAddon() override;

    void activate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry &entry,
                    fcitx::InputContextEvent &event) override;
    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;
    const fcitx::Configuration *getConfigForInputMethod(
        const fcitx::InputMethodEntry &entry) const override;
    void setConfigForInputMethod(const fcitx::InputMethodEntry &entry,
                                 const fcitx::RawConfig &config) override;
    std::string subMode(const fcitx::InputMethodEntry &entry,
                        fcitx::InputContext &input_context) override;
    std::string subModeIconImpl(
        const fcitx::InputMethodEntry &entry,
        fcitx::InputContext &input_context) override;
    std::string subModeLabelImpl(
        const fcitx::InputMethodEntry &entry,
        fcitx::InputContext &input_context) override;

private:
    class ModeAction;
    class ConfigAction;
    class EmojiAction;

    struct ModeHotkeys {
        fcitx::Key cycle{"Control+Alt+u"};
        fcitx::Key off;
        fcitx::Key emoji{"Control+Alt+period"};
    };

    struct RuntimeResources {
        config::Snapshot configuration{config::defaults()};
        core::TypingConvenienceOptions typing_options;
        macro::Snapshot snapshot;
        std::uint64_t generation{};
        keymap::Snapshot keymap_snapshot;
        std::uint64_t keymap_generation{};
        dictionary::Snapshot dictionary_snapshot;
        std::uint64_t dictionary_generation{};
        bool verified_direct_enabled{
            verified_direct_enabled_by_default};
        DirectStrategy direct_strategy{DirectStrategy::fast};
        std::string developer_route_override;
        bool emoji_enabled{};
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
    [[nodiscard]] StatusSnapshot statusSnapshotFor(
        fcitx::InputContext *input_context) const;
    void applyStatusCommand(fcitx::InputContext *input_context,
                            StatusCommand command);
    void openEmojiPicker(fcitx::InputContext *input_context);
    void clearEmojiHistory(fcitx::InputContext *input_context);
    [[nodiscard]] std::string statusIcon(
        fcitx::InputContext *input_context) const;

    fcitx::Instance &instance_;
    UinputBackspaceDevice uinput_device_;
    fcitx::FactoryFor<InputContextState> state_factory_;
    std::unique_ptr<fcitx::Menu> mode_menu_;
    std::unique_ptr<ModeAction> mode_action_;
    std::unique_ptr<ModeAction> adaptive_mode_action_;
    std::unique_ptr<ModeAction> off_mode_action_;
    std::unique_ptr<ConfigAction> telex_action_;
    std::unique_ptr<ConfigAction> vni_action_;
    std::unique_ptr<ConfigAction> viqr_action_;
    std::unique_ptr<ConfigAction> utf8_action_;
    std::unique_ptr<ConfigAction> spell_action_;
    std::unique_ptr<ConfigAction> macro_action_;
    std::unique_ptr<ConfigAction> dictionary_action_;
    std::unique_ptr<EmojiAction> emoji_action_;
    std::unique_ptr<EmojiAction> clear_emoji_history_action_;
    std::unique_ptr<EmojiPicker> emoji_picker_;
    mutable std::map<std::string, InputMethodConfig> input_method_configs_;
    mutable std::map<std::string, RuntimeResources> runtime_resources_;
    mutable bool legacy_policy_warning_emitted_{};
};

class UniLumeFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace unilume::fcitx5
