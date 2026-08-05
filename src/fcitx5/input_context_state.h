// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "adaptive_router.h"
#include "capability_builder.h"
#include "diagnostic_trace.h"
#include "direct_commit_controller.h"
#include "dictionary_contract.h"
#include "developer_route_override.h"
#include "fcitx_replacement_backend.h"
#include "input_mode_policy.h"
#include "preedit_fallback_controller.h"
#include "route_health_registry.h"
#include "macro_contract.h"
#include "keymap_contract.h"
#include "application_policy.h"

#include <fcitx-utils/key.h>
#include <fcitx/inputcontextproperty.h>

#include <cstdint>
#include <optional>
#include <string>

namespace unilume::fcitx5 {

struct MappedKey;

class InputContextState final : public fcitx::InputContextProperty {
public:
    explicit InputContextState(fcitx::InputContext &input_context,
                               UinputBackspaceDevice &uinput_device,
                               UlInputMethod method = UL_INPUT_METHOD_TELEX);
    ~InputContextState();

    void keyEvent(fcitx::KeyEvent &event);
    void reset();
    void focusReset();
    void suspendComposition();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);
    void setTypingOptions(
        const core::TypingConvenienceOptions &options);
    void setMacros(const macro::Snapshot &snapshot,
                   std::uint64_t generation);
    void setKeymap(const keymap::Snapshot &snapshot,
                   std::uint64_t generation);
    void setDictionary(const dictionary::Snapshot &snapshot,
                       std::uint64_t generation);
    void setVerifiedDirectEnabled(bool enabled);
    void setDeveloperRouteOverride(DeveloperRouteOverride override);
    void setDirectStrategy(DirectStrategy strategy);
    [[nodiscard]] DirectStrategy directStrategy() const;
    void setApplicationPolicy(const policy::Resolution &resolution,
                              std::uint64_t generation,
                              std::string_view application_identity);
    [[nodiscard]] bool applicationPolicyIsCurrent(
        std::uint64_t generation,
        std::string_view application_identity) const;
    void selectApplicationMode(policy::ApplicationMode mode);
    void cycleApplicationMode();
    [[nodiscard]] policy::ApplicationMode requestedApplicationMode() const;
    [[nodiscard]] platform::InputPath effectiveInputPath() const;
    [[nodiscard]] std::uint64_t applicationModeRevision() const;
    [[nodiscard]] bool hasApplicationModeOverride() const;
    [[nodiscard]] policy::ResolutionSource applicationPolicySource() const;
    [[nodiscard]] std::string_view applicationPolicyPattern() const;

private:
    void compositionBoundary();
    void startPendingAcknowledgedReplacement();
    void synchronizeMode();
    void synchronizeAdaptive(policy::ApplicationMode requested);
    void synchronizeLegacy(platform::InputPath previous,
                           policy::ApplicationMode requested);
    void applyModeChange(platform::InputPath previous,
                         platform::InputPath current);
    void resetRouteForCompositionEnd();
    [[nodiscard]] platform::InputPath mapAdaptivePath(
        platform::AdaptivePath path) const;
    void resolveHealthIdentity();
    void handleUncertainCompletion();
    void handlePreeditEvent(fcitx::KeyEvent &event,
                            const MappedKey &mapped,
                            std::uint64_t started_at_ns);
    void commitPendingPreedit();
    void updatePreedit();
    void clearPreedit();

    fcitx::InputContext &input_context_;
    FcitxReplacementBackend backend_;
    core::DirectCommitController direct_controller_;
    core::PreeditFallbackController preedit_controller_;
    platform::InputModePolicy mode_policy_;
    platform::RouteHealthRegistry health_registry_;
    platform::RouteDecision current_route_;
    platform::RouteHealthKey current_health_key_{};
    std::uint64_t context_id_{};
    std::string frontend_;
    std::string display_;
    bool health_identity_resolved_{};
    DiagnosticTrace diagnostics_;
    UlInputMethod input_method_{UL_INPUT_METHOD_TELEX};
    UlEngineOptions options_{1, 1, 0, 1};
    core::TypingConvenienceOptions typing_options_;
    std::uint64_t macro_generation_{};
    std::uint64_t keymap_generation_{};
    std::uint64_t dictionary_generation_{};
    bool verified_direct_enabled_{true};
    DeveloperRouteOverride developer_route_override_{};
    bool direct_replacement_available_{};
    DirectStrategy direct_strategy_{DirectStrategy::fast};
    policy::ApplicationMode policy_mode_{
        policy::ApplicationMode::adaptive};
    policy::ResolutionSource policy_source_{
        policy::ResolutionSource::missing_identity};
    std::optional<policy::ApplicationMode> application_mode_override_;
    std::string application_identity_;
    std::string policy_pattern_;
    std::uint64_t policy_generation_{};
    std::uint64_t application_mode_revision_{};
    bool policy_initialized_{};
    fcitx::Key initial_release_key_;
};

} // namespace unilume::fcitx5
