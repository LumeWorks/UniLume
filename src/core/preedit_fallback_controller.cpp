// SPDX-License-Identifier: GPL-2.0-or-later

#include "preedit_fallback_controller.h"

#include <cctype>

namespace unilume::core {
namespace {

constexpr std::size_t maximum_transactional_preedit_bytes = 2048;

bool isWhitespace(const KeyInput &input)
{
    return input.kind == KeyKind::text &&
           input.text.size() == 1 &&
           std::isspace(
               static_cast<unsigned char>(input.text.front())) != 0;
}

} // namespace

PreeditFallbackController::PreeditFallbackController(
    UlInputMethod method,
    PreeditCommitPolicy commit_policy)
    : engine_(method),
      commit_policy_(commit_policy)
{
}

PreeditAction PreeditFallbackController::submit(const KeyInput &input)
{
    commit_.clear();
    if (input.kind == KeyKind::reset ||
        input.kind == KeyKind::navigation ||
        input.has_control_modifier) {
        reset();
        return {};
    }

    if (detached_preedit_) {
        if (input.kind == KeyKind::backspace) {
            if (preedit_.empty()) {
                detached_preedit_ = false;
                return {};
            }
            preedit_.erase(previousCharacter(preedit_, preedit_.size()));
            if (preedit_.empty()) {
                detached_preedit_ = false;
            }
            return {true, commit_, preedit_};
        }
        commitPending({});
        engine_.reset();
    }

    const KeyResult result = engine_.process(input);
    if (!result.handled) {
        if (input.kind == KeyKind::backspace &&
            commit_policy_ == PreeditCommitPolicy::composition_boundary &&
            restoreBeforeBoundary()) {
            return {true, commit_, preedit_};
        }
        return {};
    }
    if (result.require_fallback) {
        commitPending(input.kind == KeyKind::text ? input.text
                                                  : std::string_view{});
        engine_.reset();
        return {true, commit_, preedit_};
    }
    if (result.commit_preedit_before) {
        commitPending({});
    }
    if (result.reset_context && isWhitespace(input)) {
        if (!applyEdit(
                result.delete_before_cursor, result.commit_text)) {
            commitPending(input.text);
            engine_.reset();
            return {true, commit_, preedit_};
        }
        if (!result.defer_preedit_commit &&
            (commit_policy_ == PreeditCommitPolicy::word_boundary ||
             preedit_.size() >= maximum_transactional_preedit_bytes)) {
            commitPending({});
        } else if (commit_policy_ ==
                   PreeditCommitPolicy::composition_boundary) {
            boundaries_.push_back({token_start_, std::move(token_inputs_)});
            token_inputs_.clear();
            token_start_ = preedit_.size();
        }
        return {true, commit_, preedit_};
    }
    if (!applyEdit(result.delete_before_cursor, result.commit_text)) {
        commitPending(input.kind == KeyKind::text ? input.text
                                                  : std::string_view{});
        engine_.reset();
    } else if (result.reset_context &&
               commit_policy_ == PreeditCommitPolicy::composition_boundary) {
        boundaries_.push_back({token_start_, std::move(token_inputs_)});
        token_inputs_.clear();
        token_start_ = preedit_.size();
    } else {
        record(input);
    }
    return {true, commit_, preedit_};
}

void PreeditFallbackController::reset()
{
    engine_.reset();
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::lineBreak()
{
    engine_.lineBreak();
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::setInputMethod(UlInputMethod method)
{
    engine_.setInputMethod(method);
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::setOptions(const UlEngineOptions &options)
{
    engine_.setOptions(options);
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::setTypingOptions(
    const TypingConvenienceOptions &options)
{
    engine_.setTypingOptions(options);
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::setMacros(const macro::Snapshot &snapshot)
{
    engine_.setMacros(snapshot);
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::setKeymap(const keymap::Snapshot &snapshot)
{
    engine_.setKeymap(snapshot);
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::setDictionary(
    const dictionary::Snapshot &snapshot)
{
    engine_.setDictionary(snapshot);
    preedit_.clear();
    commit_.clear();
    clearEditingState();
}

void PreeditFallbackController::lineBreak()
{
    engine_.lineBreak();
    preedit_.clear();
    commit_.clear();
}

void PreeditFallbackController::setInputMethod(UlInputMethod method)
{
    engine_.setInputMethod(method);
    preedit_.clear();
    commit_.clear();
}

void PreeditFallbackController::setOptions(const UlEngineOptions &options)
{
    engine_.setOptions(options);
    preedit_.clear();
    commit_.clear();
}

void PreeditFallbackController::setTypingOptions(
    const TypingConvenienceOptions &options)
{
    engine_.setTypingOptions(options);
    preedit_.clear();
    commit_.clear();
}

void PreeditFallbackController::setMacros(const macro::Snapshot &snapshot)
{
    engine_.setMacros(snapshot);
    preedit_.clear();
    commit_.clear();
}

void PreeditFallbackController::setKeymap(const keymap::Snapshot &snapshot)
{
    engine_.setKeymap(snapshot);
    preedit_.clear();
    commit_.clear();
}

void PreeditFallbackController::setDictionary(
    const dictionary::Snapshot &snapshot)
{
    engine_.setDictionary(snapshot);
    preedit_.clear();
    commit_.clear();
}

std::string_view PreeditFallbackController::preedit() const
{
    return preedit_;
}

bool PreeditFallbackController::applyEdit(
    std::int32_t delete_before_cursor,
    std::string_view commit_text)
{
    return applyEdit(preedit_, delete_before_cursor, commit_text);
}

bool PreeditFallbackController::applyEdit(
    std::string &text,
    std::int32_t delete_before_cursor,
    std::string_view commit_text)
{
    if (delete_before_cursor < 0) {
        return false;
    }
    std::size_t position = text.size();
    for (std::int32_t count = 0;
         count < delete_before_cursor;
         ++count) {
        if (position == 0) {
            return false;
        }
        position = previousCharacter(text, position);
    }
    text.erase(position);
    text.append(commit_text);
    return true;
}

KeyInput PreeditFallbackController::StoredInput::view() const
{
    return {kind, text, shift_pressed, caps_lock_on, has_control_modifier};
}

bool PreeditFallbackController::restoreBeforeBoundary()
{
    if (boundaries_.empty() || preedit_.empty()) {
        return false;
    }

    BoundaryCheckpoint checkpoint = std::move(boundaries_.back());
    boundaries_.pop_back();
    preedit_.erase(previousCharacter(preedit_, preedit_.size()));

    // A transactional preedit can contain several completed words even
    // though the engine only retains the active word. Replay that word's
    // owned inputs after deleting the boundary so tone/backspace behavior is
    // identical to the state immediately before the boundary.
    engine_.reset();
    std::string replayed;
    bool replay_ok = true;
    for (const StoredInput &stored : checkpoint.token_inputs) {
        const KeyResult result = engine_.process(stored.view());
        if (!result.handled || result.require_fallback ||
            result.reset_context || result.commit_preedit_before ||
            !applyEdit(replayed,
                       result.delete_before_cursor,
                       result.commit_text)) {
            replay_ok = false;
            break;
        }
    }

    if (replay_ok && checkpoint.token_start <= preedit_.size() &&
        preedit_.substr(checkpoint.token_start) == replayed) {
        token_start_ = checkpoint.token_start;
        token_inputs_ = std::move(checkpoint.token_inputs);
        detached_preedit_ = false;
        return true;
    }

    // Macro and convenience expansion can make the visible token differ from
    // raw replay. Keep editing the owned UTF-8 preedit locally, then hand it
    // off atomically before accepting new composition input.
    engine_.reset();
    token_inputs_.clear();
    boundaries_.clear();
    token_start_ = preedit_.size();
    detached_preedit_ = true;
    return true;
}

void PreeditFallbackController::record(const KeyInput &input)
{
    token_inputs_.push_back({
        input.kind,
        std::string(input.text),
        input.shift_pressed,
        input.caps_lock_on,
        input.has_control_modifier,
    });
}

void PreeditFallbackController::clearEditingState()
{
    token_inputs_.clear();
    boundaries_.clear();
    token_start_ = 0;
    detached_preedit_ = false;
}

void PreeditFallbackController::commitPending(std::string_view suffix)
{
    commit_.assign(preedit_);
    commit_.append(suffix);
    preedit_.clear();
    clearEditingState();
}

std::size_t PreeditFallbackController::previousCharacter(
    std::string_view text,
    std::size_t position)
{
    --position;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    return position;
}

} // namespace unilume::core
