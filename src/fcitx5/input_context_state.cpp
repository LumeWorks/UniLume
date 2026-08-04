// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_context_state.h"

#include "fcitx_key_mapper.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <cstdint>
#include <string>

namespace unilume::fcitx5 {

namespace {

// Reduce the 16-byte input-context UUID to a 64-bit health key component.
// A simple FNV-1a walk is enough: the value only needs to be stable for a
// given context and distinguish one context from another on the same
// frontend/display.
std::uint64_t hashContextUuid(const std::array<std::uint8_t, 16> &uuid)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : uuid) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

InputContextState::InputContextState(fcitx::InputContext &input_context,
                                     UinputBackspaceDevice &uinput_device,
                                     UlInputMethod method)
    : input_context_(input_context),
      backend_(input_context, uinput_device),
      direct_controller_(backend_, method),
      preedit_controller_(
          method,
          core::PreeditCommitPolicy::composition_boundary),
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
            if (sequence == 0) {
                direct_controller_.timeout(
                    direct_controller_.activeSequence());
                return;
            }
            // A guarded sentinel ACK, even with a validated surrounding
            // snapshot, does not prove the target application applied the
            // delete-and-insert.  A valid snapshot is not sufficient proof
            // by itself per Issue #119, so the outcome must be uncertain.
            direct_controller_.complete(
                sequence, platform::ReplacementOutcome::uncertain);
            handleUncertainCompletion();
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

    // Only key presses that can reach an engine may change composition
    // ownership. Release events above never cause a direct/preedit handoff.
    synchronizeMode();

    if (mapped.status == MappingStatus::shortcut_fence ||
        mapped.status == MappingStatus::reset) {
        diagnostics_.recordReset(
            mapped.has_control_modifier
                ? TraceResetReason::control_shortcut
                : TraceResetReason::navigation);
        if (mode_policy_.path() == platform::InputPath::preedit) {
            commitPendingPreedit();
        }
        direct_controller_.resetForFocus();
        initial_release_key_ = fcitx::Key();
        resetRouteForCompositionEnd();
        return;
    }

    if (mapped.status == MappingStatus::line_break) {
        diagnostics_.recordReset(TraceResetReason::navigation);
        if (mode_policy_.path() == platform::InputPath::preedit) {
            commitPendingPreedit();
            preedit_controller_.lineBreak();
            clearPreedit();
        }
        direct_controller_.lineBreak();
        initial_release_key_ = fcitx::Key();
        resetRouteForCompositionEnd();
        return;
    }

    if (mode_policy_.path() == platform::InputPath::off) {
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        handlePreeditEvent(event, mapped, started_at_ns);
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
                if (sequence == 0) {
                    direct_controller_.timeout(
                        direct_controller_.activeSequence());
                    return;
                }
                // A fast sentinel ACK only proves the synthetic press/release
                // pair went through the frontend.  It does not prove the
                // target application applied the delete-and-insert, so it
                // must be uncertain per Issue #119.
                direct_controller_.complete(
                    sequence, platform::ReplacementOutcome::uncertain);
                handleUncertainCompletion();
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
    // resets are ignored while the bounded deletion sequence is returning.
    if (backend_.acknowledgedDeletionPending() &&
        !backend_.initialBackspacePending()) {
        return;
    }
    focusReset();
}

void InputContextState::focusReset()
{
    diagnostics_.recordReset(TraceResetReason::focus);
    direct_controller_.resetForFocus();
    preedit_controller_.reset();
    clearPreedit();
    initial_release_key_ = fcitx::Key();
    backend_.clearFailure();
    mode_policy_.reset();
    // Clear the active route but NOT the health registry: quarantine
    // survives a focus reset when the context+signature are unchanged
    // (Issue #127).  The next composition re-routes and re-checks it.
    current_route_ = {};
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

    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setInputMethod(method);
    preedit_controller_.setInputMethod(method);
    clearPreedit();
    resetRouteForCompositionEnd();
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
    resetRouteForCompositionEnd();
    options_ = options;
}

void InputContextState::setTypingOptions(
    const core::TypingConvenienceOptions &options)
{
    if (options == typing_options_) {
        return;
    }
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.setTypingOptions(options);
    preedit_controller_.setTypingOptions(options);
    clearPreedit();
    resetRouteForCompositionEnd();
    typing_options_ = options;
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
    resetRouteForCompositionEnd();
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
    resetRouteForCompositionEnd();
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
    resetRouteForCompositionEnd();
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
    case policy::ApplicationMode::adaptive:
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
    if (mode_policy_.path() == platform::InputPath::preedit) {
        commitPendingPreedit();
    }
    direct_controller_.resetForFocus();
    preedit_controller_.reset();
    clearPreedit();
    initial_release_key_ = fcitx::Key();
    mode_policy_.reset();
    current_route_ = {};
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
            direct_controller_.activeSequence(),
            platform::ReplacementOutcome::not_applied);
    }
}

void InputContextState::synchronizeMode()
{
    const platform::InputPath previous = mode_policy_.path();
    direct_replacement_available_ =
        backend_.supportsDirectReplacement();
    const policy::ApplicationMode requested = requestedApplicationMode();

    // Adaptive (and the legacy `automatic` alias) is the mode driven by
    // the adaptive router.  The explicit modes (direct / safe_preedit /
    // off) keep the legacy boolean observe() contract so their
    // behaviour, including the experimental uinput direct path, is
    // unchanged.  `automatic` still routes through the router for
    // backwards compatibility with pre-migration configs.
    if (requested == policy::ApplicationMode::adaptive ||
        requested == policy::ApplicationMode::automatic) {
        synchronizeAdaptive(requested);
        return;
    }
    synchronizeLegacy(previous, requested);
}

void InputContextState::synchronizeLegacy(
    platform::InputPath previous,
    policy::ApplicationMode requested)
{
    // The adaptive router is not authoritative here: clear any stale
    // router state so quarantine is never consulted for explicit modes.
    current_route_ = {};

    const bool direct_available =
        verified_direct_enabled_ && direct_replacement_available_;
    const platform::InputPath current =
        mode_policy_.observe(requested, direct_available);
    applyModeChange(previous, current);
}

void InputContextState::synchronizeAdaptive(
    policy::ApplicationMode requested)
{
    const ReplacementObservation &obs = backend_.lastObservation();

    // Resolve replacement semantics from the refreshed observation.
    // Uinput (split_unverified) is reflected so the router can decide;
    // the router never selects it as atomic_direct for Automatic, which
    // is the contract of Issue #127.  Poison and the global verified
    // toggle both suppress the atomic path.
    platform::ReplacementSemantics replacement =
        platform::ReplacementSemantics::none;
    if (verified_direct_enabled_ && !backend_.poisoned()) {
        if (obs.atomic_transport) {
            replacement =
                platform::ReplacementSemantics::client_atomic_event;
        } else if (obs.acknowledged_uinput) {
            replacement =
                platform::ReplacementSemantics::split_unverified;
        }
    }

    const platform::PreeditSemantics preedit =
        input_context_.capabilityFlags().test(
            fcitx::CapabilityFlag::Preedit)
            ? platform::PreeditSemantics::client
            : platform::PreeditSemantics::server;

    const bool snapshot_valid =
        obs.cursor_valid && obs.utf8_valid &&
        obs.within_resource_limit;
    // Selection-collapse is not yet observed from the frontend; default to
    // collapsed so the atomic path is not falsely blocked.  A follow-up
    // can read the surrounding-text anchor to refine this.
    //
    // `obs.surrounding_available` is the client-advertised
    // CapabilityFlag::SurroundingText — a STABLE support capability, not
    // whether the current snapshot has text.  It feeds the capability
    // signature (via buildCapabilities) so quarantine does not lift when
    // only the snapshot content changes (cursor moves, text edited); the
    // transient `snapshot_valid` is excluded from the signature.
    const platform::InputCapabilities caps = platform::buildCapabilities(
        replacement, preedit, obs.surrounding_available, snapshot_valid,
        true, obs.generation);

    resolveHealthIdentity();
    const platform::RouteHealthKey key{
        context_id_, frontend_, display_,
        caps.replacement, caps.signature};
    const bool quarantined = health_registry_.isQuarantined(key);

    // Composition stickiness: a non-passthrough route is held to the
    // composition boundary.  A capability loss that makes the current
    // route unsafe fences first, then re-routes.
    if (platform::AdaptiveRouter::shouldHoldRoute(current_route_, caps)) {
        if (platform::AdaptiveRouter::requiresFence(
                current_route_, caps)) {
            compositionBoundary();
        } else {
            return;
        }
    }

    const platform::RouteDecision decision =
        platform::AdaptiveRouter::route(caps, quarantined, false);
    current_route_ = decision;
    current_health_key_ = key;

    // All preedit paths in Automatic mode commit at word boundary so the
    // preedit does not accumulate a long underlined phrase across spaces.
    // This applies to both client preedit (Chrome/Electron show the
    // composition_boundary preedit as a long underlined string) and server
    // preedit (the input panel popup would accumulate the whole phrase).
    // Boundary backspace restore is a nice-to-have that composition_boundary
    // provides in Qt apps, but the long-preedit UX is worse for most apps.
    if (decision.path == platform::AdaptivePath::client_preedit ||
        decision.path == platform::AdaptivePath::server_preedit) {
        preedit_controller_.setCommitPolicy(
            core::PreeditCommitPolicy::word_boundary);
    }

    const platform::InputPath previous_now = mode_policy_.path();
    const platform::InputPath current = mapAdaptivePath(decision.path);
    mode_policy_.assignPath(current, requested);
    applyModeChange(previous_now, current);
}

void InputContextState::applyModeChange(
    platform::InputPath previous,
    platform::InputPath current)
{
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
            current == platform::InputPath::preedit,
            backend_.lastObservation());
    }
}

platform::InputPath InputContextState::mapAdaptivePath(
    platform::AdaptivePath path) const
{
    switch (path) {
    case platform::AdaptivePath::atomic_direct:
        return platform::InputPath::direct;
    case platform::AdaptivePath::client_preedit:
    case platform::AdaptivePath::server_preedit:
        return platform::InputPath::preedit;
    case platform::AdaptivePath::passthrough:
        return platform::InputPath::off;
    }
    return platform::InputPath::off;
}

void InputContextState::resetRouteForCompositionEnd()
{
    mode_policy_.resetForCompositionEnd();
    current_route_ = {};
}

void InputContextState::resolveHealthIdentity()
{
    if (health_identity_resolved_) {
        return;
    }
    context_id_ = hashContextUuid(input_context_.uuid());
    frontend_.assign(input_context_.frontend());
    display_ = input_context_.display();
    health_identity_resolved_ = true;
}

void InputContextState::handleUncertainCompletion()
{
    // An uncertain terminal proves the atomic route is untrustworthy for
    // this transport+capability signature.  Quarantine it so the next
    // composition with the same signature falls back to preedit instead
    // of retrying atomic direct.  The quarantine survives focus resets
    // (per Issue #127) until the capability signature changes.
    if (current_route_.path == platform::AdaptivePath::atomic_direct) {
        health_registry_.quarantine(
            current_health_key_,
            backend_.lastObservation().generation);
    }
    // The uncertain terminal ends the composition: clear the route so
    // the next key re-routes and consults the registry.
    current_route_ = {};
}

void InputContextState::handlePreeditEvent(
    fcitx::KeyEvent &event,
    const MappedKey &mapped,
    std::uint64_t started_at_ns)
{
    const core::PreeditAction action =
        preedit_controller_.submit(mapped.input());
    if (!action.commit_text.empty()) {
        diagnostics_.recordPreeditHandoff(backend_.lastObservation());
        input_context_.commitString(std::string(action.commit_text));
    }
    if (preedit_controller_.preedit().empty()) {
        resetRouteForCompositionEnd();
    }
    updatePreedit();
    diagnostics_.recordPreedit(
        action, backend_.lastObservation(), started_at_ns);
    if (action.handled) {
        event.filterAndAccept();
    }
}

void InputContextState::commitPendingPreedit()
{
    const std::string_view pending = preedit_controller_.preedit();
    if (!pending.empty()) {
        diagnostics_.recordPreeditHandoff(backend_.lastObservation());
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
        input_context_.capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    if (!value.empty()) {
        text.append(
            std::string(value),
            requestedApplicationMode() == policy::ApplicationMode::safe_preedit
                ? fcitx::TextFormatFlag::Underline
                : fcitx::TextFormatFlag::NoFlag);
    }
    text.setCursor(static_cast<int>(value.size()));
    if (client_preedit) {
        input_context_.inputPanel().setClientPreedit(text);
        input_context_.updatePreedit();
    } else {
        input_context_.inputPanel().setPreedit(text);
        input_context_.updateUserInterface(
            fcitx::UserInterfaceComponent::InputPanel);
    }
}

void InputContextState::clearPreedit()
{
    input_context_.inputPanel().reset();
    const bool client_preedit =
        input_context_.capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    if (client_preedit) {
        input_context_.updatePreedit();
    } else {
        input_context_.updateUserInterface(
            fcitx::UserInterfaceComponent::InputPanel);
    }
}

} // namespace unilume::fcitx5
