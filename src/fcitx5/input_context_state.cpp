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
    if (mode_policy_.path() == platform::InputPath::off) {
        return;
    }
    if (mapped.status == MappingStatus::reset) {
        diagnostics_.recordReset(TraceResetReason::navigation);
        if (mode_policy_.path() == platform::InputPath::preedit) {
            commitPendingPreedit();
        }
        direct_controller_.resetForFocus();
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
    clearPreedit();
    mode_policy_.reset();
    if (application_mode_override_) {
        application_mode_override_.reset();
        ++application_mode_revision_;
    }
    synchronizeMode();
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
    clearPreedit();
    mode_policy_.resetForCompositionEnd();
    keymap_generation_ = generation;
}

void InputContextState::setDictionary(
    const dictionary::Snapshot &snapshot,
    std::uint64_t generation)
{
    if (generation == dictionary_generation_) {
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setDictionary(snapshot);
    preedit_controller_.setDictionary(snapshot);
    clearPreedit();
    mode_policy_.resetForCompositionEnd();
    dictionary_generation_ = generation;
}

void InputContextState::setVerifiedDirectEnabled(bool enabled)
{
    if (enabled == verified_direct_enabled_) {
        return;
    }
    compositionBoundary();
    verified_direct_enabled_ = enabled;
    ++application_mode_revision_;
    synchronizeMode();
}

void InputContextState::setApplicationPolicy(
    const policy::Resolution &resolution,
    std::uint64_t generation,
    std::string_view application_identity)
{
    if (policy_initialized_ &&
        generation == policy_generation_ &&
        application_identity == application_identity_ &&
        resolution.mode == policy_mode_ &&
        resolution.source == policy_source_ &&
        resolution.pattern == policy_pattern_) {
        return;
    }
    compositionBoundary();
    policy_mode_ = resolution.mode;
    policy_source_ = resolution.source;
    application_identity_.assign(application_identity);
    policy_pattern_.assign(resolution.pattern);
    policy_generation_ = generation;
    policy_initialized_ = true;
    application_mode_override_.reset();
    ++application_mode_revision_;
    synchronizeMode();
}

bool InputContextState::applicationPolicyIsCurrent(
    std::uint64_t generation,
    std::string_view application_identity) const
{
    return policy_initialized_ &&
           generation == policy_generation_ &&
           application_identity == application_identity_;
}

void InputContextState::selectApplicationMode(policy::ApplicationMode mode)
{
    if (application_mode_override_ &&
        *application_mode_override_ == mode) {
        return;
    }
    compositionBoundary();
    application_mode_override_ = mode;
    ++application_mode_revision_;
    synchronizeMode();
}

void InputContextState::cycleApplicationMode()
{
    switch (requestedApplicationMode()) {
    case policy::ApplicationMode::automatic:
        selectApplicationMode(policy::ApplicationMode::direct);
        break;
    case policy::ApplicationMode::direct:
        selectApplicationMode(policy::ApplicationMode::safe_preedit);
        break;
    case policy::ApplicationMode::safe_preedit:
        selectApplicationMode(policy::ApplicationMode::off);
        break;
    case policy::ApplicationMode::off:
        selectApplicationMode(policy::ApplicationMode::automatic);
        break;
    }
}

policy::ApplicationMode InputContextState::requestedApplicationMode() const
{
    return application_mode_override_.value_or(policy_mode_);
}

platform::InputPath InputContextState::effectiveInputPath() const
{
    return mode_policy_.path();
}

std::uint64_t InputContextState::applicationModeRevision() const
{
    return application_mode_revision_;
}

std::string InputContextState::applicationModeReason() const
{
    if (application_mode_override_) {
        return "selected for this input context";
    }
    switch (policy_source_) {
    case policy::ResolutionSource::missing_identity:
        return "safe preedit: application identity unavailable";
    case policy::ResolutionSource::exact_rule:
        return "matched exact rule " + policy_pattern_;
    case policy::ResolutionSource::prefix_rule:
        return "matched prefix rule " + policy_pattern_ + '*';
    case policy::ResolutionSource::default_rule:
        return "application policy default";
    }
    return {};
}

void InputContextState::compositionBoundary()
{
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.resetForFocus();
    preedit_controller_.reset();
    clearPreedit();
    mode_policy_.reset();
}

void InputContextState::synchronizeMode()
{
    const platform::InputPath previous = mode_policy_.path();
    const platform::InputPath current = mode_policy_.observe(
        requestedApplicationMode(),
        verified_direct_enabled_ &&
            backend_.supportsDirectReplacement());
    if (previous == current) {
        return;
    }
    if (current == platform::InputPath::preedit &&
        previous == platform::InputPath::direct) {
        diagnostics_.recordReset(
            TraceResetReason::capability_loss);
        direct_controller_.resetForFocus();
    }
    preedit_controller_.reset();
    clearPreedit();
    if (current != platform::InputPath::off) {
        diagnostics_.recordModeChange(
            current == platform::InputPath::preedit);
    }
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
