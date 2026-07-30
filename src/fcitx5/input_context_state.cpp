// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_context_state.h"

#include "fcitx_key_mapper.h"

#include <string>

namespace unilume::fcitx5 {

InputContextState::InputContextState(fcitx::InputContext &input_context,
                                     UinputBackspaceDevice &uinput_device,
                                     UlInputMethod method)
    : input_context_(input_context),
      backend_(input_context, uinput_device),
      direct_controller_(backend_, method),
      input_method_(method)
{
}

InputContextState::~InputContextState()
{
    diagnostics_.flush();
}

void InputContextState::keyEvent(fcitx::KeyEvent &event)
{
    const fcitx::Key raw_key = event.rawKey();
    const fcitx::KeyStates raw_states = raw_key.states();
    const bool has_shortcut_modifier =
        raw_states.test(fcitx::KeyState::Shift) ||
        raw_states.test(fcitx::KeyState::Ctrl) ||
        raw_states.test(fcitx::KeyState::Alt) ||
        raw_states.testAny(fcitx::KeyState::Ctrl_Alt) ||
        raw_states.test(fcitx::KeyState::Super) ||
        raw_states.test(fcitx::KeyState::Super2) ||
        raw_states.test(fcitx::KeyState::Hyper) ||
        raw_states.test(fcitx::KeyState::Hyper2) ||
        raw_states.test(fcitx::KeyState::Meta) ||
        raw_states.test(fcitx::KeyState::Mod5);
    const bool is_plain_backspace =
        (raw_key.sym() == FcitxKey_BackSpace || raw_key.sym() == 8) &&
        !has_shortcut_modifier;
    const bool matching_initial_release =
        raw_key.code() != 0 && initial_release_key_.code() != 0
            ? raw_key.code() == initial_release_key_.code()
            : raw_key.sym() == initial_release_key_.sym();

    // Transaction releases must be routed before the normal mapper, whose
    // contract deliberately ignores releases. The triggering release may be
    // any printable key; synthetic ACKs are only unmodified Backspace.
    if (event.isRelease()) {
        if (backend_.initialBackspacePending() &&
            matching_initial_release) {
            event.filterAndAccept();
            startPendingAcknowledgedReplacement();
            return;
        }
        if (!is_plain_backspace) {
            return;
        }
        if (backend_.consumeCancelledBackspace(true)) {
            event.filterAndAccept();
            return;
        }
        if (backend_.consumeFastSentinelRelease()) {
            event.filterAndAccept();
            return;
        }
        if (!backend_.acknowledgedDeletionPending()) {
            return;
        }
        switch (backend_.acknowledgeBackspaceRelease()) {
        case BackspaceReleaseAcknowledgement::forward_deletion:
            if (backend_.consumeUncertainDispatch()) {
                direct_controller_.timeout(
                    direct_controller_.activeSequence());
            }
            return;
        case BackspaceReleaseAcknowledgement::consume_sentinel:
            event.filterAndAccept();
            return;
        case BackspaceReleaseAcknowledgement::complete_guarded: {
            event.filterAndAccept();
            if (!backend_.guardedBoundaryValid()) {
                direct_controller_.timeout(
                    direct_controller_.activeSequence());
                return;
            }
            const std::uint64_t sequence =
                backend_.finishAcknowledgedReplacement();
            direct_controller_.complete(sequence, sequence != 0);
            if (backend_.initialBackspacePending()) {
                startPendingAcknowledgedReplacement();
            }
            return;
        }
        case BackspaceReleaseAcknowledgement::unexpected:
            event.filterAndAccept();
            direct_controller_.timeout(
                direct_controller_.activeSequence());
            return;
        }
    }

    const std::uint64_t started_at_ns = diagnostics_.beginEvent();
    const MappedKey mapped = mapKeyEvent(event);
    if (mapped.status == MappingStatus::ignored) {
        return;
    }

    if (mapped.status == MappingStatus::shortcut_fence ||
        mapped.status == MappingStatus::reset) {
        diagnostics_.recordReset(
            mapped.has_control_modifier
                ? TraceResetReason::control_shortcut
                : TraceResetReason::navigation);
        direct_controller_.resetForFocus();
        initial_release_key_ = fcitx::Key();
        mode_policy_.resetForCompositionEnd();
        return;
    }

    if (mapped.status == MappingStatus::line_break) {
        diagnostics_.recordReset(TraceResetReason::navigation);
        direct_controller_.lineBreak();
        initial_release_key_ = fcitx::Key();
        mode_policy_.resetForCompositionEnd();
        return;
    }

    if (mapped.status == MappingStatus::plain_backspace) {
        if (backend_.consumeCancelledBackspace(false)) {
            event.filterAndAccept();
            return;
        }
        if (backend_.acknowledgedDeletionPending()) {
            switch (backend_.acknowledgeBackspace()) {
            case BackspaceAcknowledgement::forward_deletion:
                return;
            case BackspaceAcknowledgement::consume_sentinel_fast: {
                event.filterAndAccept();
                const std::uint64_t sequence =
                    backend_.finishAcknowledgedReplacement();
                direct_controller_.complete(sequence, sequence != 0);
                if (backend_.initialBackspacePending()) {
                    startPendingAcknowledgedReplacement();
                }
                return;
            }
            case BackspaceAcknowledgement::consume_sentinel_guarded:
                event.filterAndAccept();
                return;
            case BackspaceAcknowledgement::unexpected:
                event.filterAndAccept();
                direct_controller_.timeout(
                    direct_controller_.activeSequence());
                return;
            }
        }
    }

    synchronizeMode();
    if (mode_policy_.path() == platform::InputPath::off) {
        return;
    }

    const bool initial_backspace_was_pending =
        backend_.initialBackspacePending();
    const core::SubmissionStatus status =
        direct_controller_.submit(mapped.input());
    if (!initial_backspace_was_pending &&
        backend_.initialBackspacePending()) {
        initial_release_key_ = event.rawKey();
    }
    if (backend_.consumeUncertainDispatch()) {
        direct_controller_.timeout(
            direct_controller_.activeSequence());
    }
    diagnostics_.recordDirect(
        status,
        direct_controller_.metrics(),
        backend_.lastObservation(),
        started_at_ns);
    if (status == core::SubmissionStatus::handled ||
        status == core::SubmissionStatus::queued) {
        event.filterAndAccept();
    } else if (status == core::SubmissionStatus::unhandled &&
               mapped.kind == core::KeyKind::backspace) {
        backend_.reset();
    }
}

void InputContextState::reset()
{
    // Some clients reset the input context after each synthetic Backspace.
    // A real deactivation uses focusReset(), so only those protocol-local
    // resets are ignored while the bounded batch is returning.
    if ((backend_.acknowledgedDeletionPending() &&
         !backend_.initialBackspacePending()) ||
        backend_.fastSentinelReleasePending()) {
        return;
    }
    focusReset();
}

void InputContextState::focusReset()
{
    diagnostics_.recordReset(TraceResetReason::focus);
    direct_controller_.resetForFocus();
    initial_release_key_ = fcitx::Key();
    backend_.clearFailure();
    mode_policy_.reset();
    if (application_mode_override_) {
        application_mode_override_.reset();
        ++application_mode_revision_;
    }
    synchronizeMode();
}

void InputContextState::suspendComposition()
{
    compositionBoundary();
}

void InputContextState::setInputMethod(UlInputMethod method)
{
    if (method == input_method_) {
        return;
    }

    direct_controller_.setInputMethod(method);
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
    direct_controller_.setOptions(options);
    mode_policy_.resetForCompositionEnd();
    options_ = options;
}

void InputContextState::setTypingOptions(
    const core::TypingConvenienceOptions &options)
{
    if (options == typing_options_) {
        return;
    }
    direct_controller_.setTypingOptions(options);
    mode_policy_.resetForCompositionEnd();
    typing_options_ = options;
}

void InputContextState::setMacros(const macro::Snapshot &snapshot,
                                  std::uint64_t generation)
{
    if (generation == macro_generation_) {
        return;
    }
    direct_controller_.setMacros(snapshot);
    mode_policy_.resetForCompositionEnd();
    macro_generation_ = generation;
}

void InputContextState::setKeymap(const keymap::Snapshot &snapshot,
                                  std::uint64_t generation)
{
    if (generation == keymap_generation_) {
        return;
    }
    direct_controller_.setKeymap(snapshot);
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
    direct_controller_.setDictionary(snapshot);
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

void InputContextState::setDirectStrategy(DirectStrategy strategy)
{
    if (strategy == direct_strategy_) {
        return;
    }
    compositionBoundary();
    backend_.setDirectStrategy(strategy);
    direct_strategy_ = strategy;
    synchronizeMode();
}

DirectStrategy InputContextState::directStrategy() const
{
    return direct_strategy_;
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
    mode = policy::normalizeMode(mode);
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
    switch (policy::normalizeMode(requestedApplicationMode())) {
    case policy::ApplicationMode::direct:
        selectApplicationMode(policy::ApplicationMode::off);
        break;
    case policy::ApplicationMode::off:
        selectApplicationMode(policy::ApplicationMode::direct);
        break;
    case policy::ApplicationMode::automatic:
    case policy::ApplicationMode::safe_preedit:
        break;
    }
}

policy::ApplicationMode InputContextState::requestedApplicationMode() const
{
    return policy::normalizeMode(
        application_mode_override_.value_or(policy_mode_));
}

platform::InputPath InputContextState::effectiveInputPath() const
{
    return mode_policy_.path();
}

std::uint64_t InputContextState::applicationModeRevision() const
{
    return application_mode_revision_;
}

bool InputContextState::hasApplicationModeOverride() const
{
    return application_mode_override_.has_value();
}

policy::ResolutionSource InputContextState::applicationPolicySource() const
{
    return policy_source_;
}

std::string_view InputContextState::applicationPolicyPattern() const
{
    return policy_pattern_;
}

void InputContextState::compositionBoundary()
{
    direct_controller_.resetForFocus();
    initial_release_key_ = fcitx::Key();
    mode_policy_.reset();
}

void InputContextState::startPendingAcknowledgedReplacement()
{
    initial_release_key_ = fcitx::Key();
    if (backend_.startAcknowledgedReplacement()) {
        return;
    }
    if (backend_.consumeUncertainDispatch()) {
        direct_controller_.timeout(direct_controller_.activeSequence());
    } else {
        direct_controller_.complete(
            direct_controller_.activeSequence(), false);
    }
}

void InputContextState::synchronizeMode()
{
    const platform::InputPath previous = mode_policy_.path();
    direct_replacement_available_ =
        backend_.supportsDirectReplacement();
    const platform::InputPath current = mode_policy_.observe(
        requestedApplicationMode(),
        verified_direct_enabled_ &&
            direct_replacement_available_);
    if (previous == current) {
        return;
    }
    if (current == platform::InputPath::off &&
        previous == platform::InputPath::direct) {
        diagnostics_.recordReset(
            TraceResetReason::capability_loss);
        direct_controller_.resetForFocus();
    }
    if (current != platform::InputPath::off) {
        diagnostics_.recordModeChange(
            false,
            backend_.lastObservation());
    }
}

} // namespace unilume::fcitx5
