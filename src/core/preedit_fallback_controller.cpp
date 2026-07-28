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

    const KeyResult result = engine_.process(input);
    if (!result.handled) {
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
        }
        return {true, commit_, preedit_};
    }
    if (!applyEdit(result.delete_before_cursor, result.commit_text)) {
        commitPending(input.kind == KeyKind::text ? input.text
                                                  : std::string_view{});
        engine_.reset();
    }
    return {true, commit_, preedit_};
}

void PreeditFallbackController::reset()
{
    engine_.reset();
    preedit_.clear();
    commit_.clear();
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
    if (delete_before_cursor < 0) {
        return false;
    }
    std::size_t position = preedit_.size();
    for (std::int32_t count = 0;
         count < delete_before_cursor;
         ++count) {
        if (position == 0) {
            return false;
        }
        position = previousCharacter(preedit_, position);
    }
    preedit_.erase(position);
    preedit_.append(commit_text);
    return true;
}

void PreeditFallbackController::commitPending(std::string_view suffix)
{
    commit_.assign(preedit_);
    commit_.append(suffix);
    preedit_.clear();
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
