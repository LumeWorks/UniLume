// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_context_state.h"

#include "fcitx_key_mapper.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <string>

namespace unilume::fcitx5 {

InputContextState::InputContextState(fcitx::InputContext &input_context,
                                     UlInputMethod method)
    : input_context_(input_context),
      backend_(input_context),
      direct_controller_(backend_, method),
      preedit_controller_(method),
      input_method_(method)
{
}

InputContextState::~InputContextState()
{
    diagnostics_.flush();
}

void InputContextState::keyEvent(fcitx::KeyEvent &event)
{
    const std::uint64_t started_at_ns = diagnostics_.beginEvent();
    const MappedKey mapped = mapKeyEvent(event);
    if (mapped.status == MappingStatus::ignored) {
        return;
    }
    // Release events may expose transient capability flags in some X11
    // frontends. Only an event that can reach the engine may change the
    // direct-commit policy.
    synchronizeMode();
    if (mapped.status == MappingStatus::reset) {
        diagnostics_.recordReset(TraceResetReason::navigation);
        if (mode_policy_.path() == platform::InputPath::preedit) {
            commitPendingPreedit();
        }
        direct_controller_.resetForFocus();
        backend_.reset();
        mode_policy_.resetForCompositionEnd();
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit &&
        mapped.has_control_modifier) {
        diagnostics_.recordReset(
            TraceResetReason::control_shortcut);
        commitPendingPreedit();
        mode_policy_.resetForCompositionEnd();
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        handlePreeditEvent(event, mapped, started_at_ns);
        return;
    }

    const core::SubmissionStatus status =
        direct_controller_.submit(mapped.input());
    diagnostics_.recordDirect(
        status,
        direct_controller_.metrics(),
        backend_.lastObservation(),
        started_at_ns);
    if (status != core::SubmissionStatus::unhandled) {
        event.filterAndAccept();
    } else if (mapped.kind == core::KeyKind::backspace) {
        backend_.reset();
    }
}

void InputContextState::reset()
{
    diagnostics_.recordReset(TraceResetReason::focus);
    direct_controller_.resetForFocus();
    preedit_controller_.reset();
    backend_.reset();
    clearPreedit();
    mode_policy_.reset();
}

void InputContextState::setInputMethod(UlInputMethod method)
{
    if (method == input_method_) {
        return;
    }

    // A mode change is a composition boundary. Commit preedit before replacing
    // either engine so that a configuration reload never drops user text.
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setInputMethod(method);
    preedit_controller_.setInputMethod(method);
    backend_.reset();
    clearPreedit();
    mode_policy_.resetForCompositionEnd();
    input_method_ = method;
}

void InputContextState::setOptions(const UlEngineOptions &options)
{
    if (options.spell_check == options_.spell_check &&
        options.free_marking == options_.free_marking &&
        options.modern_tone == options_.modern_tone &&
        options.auto_restore == options_.auto_restore) {
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setOptions(options);
    preedit_controller_.setOptions(options);
    backend_.reset();
    clearPreedit();
    mode_policy_.resetForCompositionEnd();
    options_ = options;
}

void InputContextState::setMacros(const macro::Snapshot &snapshot,
                                  std::uint64_t generation)
{
    if (generation == macro_generation_) {
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setMacros(snapshot);
    preedit_controller_.setMacros(snapshot);
    backend_.reset();
    clearPreedit();
    mode_policy_.resetForCompositionEnd();
    macro_generation_ = generation;
}

void InputContextState::setKeymap(const keymap::Snapshot &snapshot,
                                  std::uint64_t generation)
{
    if (generation == keymap_generation_) {
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setKeymap(snapshot);
    preedit_controller_.setKeymap(snapshot);
    backend_.reset();
    clearPreedit();
    mode_policy_.resetForCompositionEnd();
    keymap_generation_ = generation;
}

void InputContextState::synchronizeMode()
{
    const platform::InputPath previous = mode_policy_.path();
    const platform::InputPath current = mode_policy_.observe(
        backend_.supportsDirectReplacement());
    if (previous == current) {
        return;
    }
    if (current == platform::InputPath::preedit) {
        diagnostics_.recordReset(
            TraceResetReason::capability_loss);
        direct_controller_.resetForFocus();
        backend_.reset();
    }
    preedit_controller_.reset();
    clearPreedit();
    diagnostics_.recordModeChange(
        current == platform::InputPath::preedit);
}

void InputContextState::handlePreeditEvent(
    fcitx::KeyEvent &event,
    const MappedKey &mapped,
    std::uint64_t started_at_ns)
{
    const core::PreeditAction action =
        preedit_controller_.submit(mapped.input());
    if (!action.commit_text.empty()) {
        input_context_.commitString(std::string(action.commit_text));
    }
    if (preedit_controller_.preedit().empty()) {
        mode_policy_.resetForCompositionEnd();
    }
    updatePreedit();
    diagnostics_.recordPreedit(
        action,
        backend_.supportsDirectReplacement(),
        started_at_ns);
    if (action.handled) {
        event.filterAndAccept();
    }
}

void InputContextState::commitPendingPreedit()
{
    const std::string_view pending = preedit_controller_.preedit();
    if (!pending.empty()) {
        input_context_.commitString(std::string(pending));
    }
    preedit_controller_.reset();
    clearPreedit();
}

void InputContextState::updatePreedit()
{
    const std::string_view value = preedit_controller_.preedit();
    fcitx::Text text;
    const bool client_preedit =
        input_context_.capabilityFlags().test(
            fcitx::CapabilityFlag::Preedit);
    if (!value.empty()) {
        text.append(
            std::string(value),
            client_preedit ? fcitx::TextFormatFlag::Underline
                           : fcitx::TextFormatFlag::NoFlag);
    }
    text.setCursor(value.size());
    if (client_preedit) {
        input_context_.inputPanel().setClientPreedit(text);
    } else {
        input_context_.inputPanel().setPreedit(text);
    }
    input_context_.updatePreedit();
    input_context_.updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
}

void InputContextState::clearPreedit()
{
    input_context_.inputPanel().reset();
    input_context_.updatePreedit();
    input_context_.updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
}

} // namespace unilume::fcitx5
