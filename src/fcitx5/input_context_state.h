// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "diagnostic_trace.h"
#include "direct_commit_controller.h"
#include "fcitx_replacement_backend.h"
#include "input_mode_policy.h"
#include "preedit_fallback_controller.h"
#include "macro_contract.h"
#include "keymap_contract.h"

#include <fcitx/inputcontextproperty.h>

namespace unilume::fcitx5 {

struct MappedKey;

class InputContextState final : public fcitx::InputContextProperty {
public:
    explicit InputContextState(fcitx::InputContext &input_context,
                               UlInputMethod method = UL_INPUT_METHOD_TELEX);
    ~InputContextState();

    void keyEvent(fcitx::KeyEvent &event);
    void reset();
    void setInputMethod(UlInputMethod method);
    void setOptions(const UlEngineOptions &options);
    void setMacros(const macro::Snapshot &snapshot,
                   std::uint64_t generation);
    void setKeymap(const keymap::Snapshot &snapshot,
                   std::uint64_t generation);

private:
    void synchronizeMode();
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
    DiagnosticTrace diagnostics_;
    UlInputMethod input_method_{UL_INPUT_METHOD_TELEX};
    UlEngineOptions options_{1, 1, 0, 1};
    std::uint64_t macro_generation_{};
    std::uint64_t keymap_generation_{};
};

} // namespace unilume::fcitx5
