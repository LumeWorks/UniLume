// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_picker.h"

#include <fcitx-module/emoji/emoji_public.h>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

#include <algorithm>
#include <memory>
#include <string>

namespace unilume::fcitx5 {
namespace {

class EmojiCandidateWord final : public fcitx::CandidateWord {
public:
    EmojiCandidateWord(EmojiPicker &picker,
                       std::string glyph,
                       std::string comment)
        : fcitx::CandidateWord(fcitx::Text(glyph)),
          picker_(picker),
          glyph_(std::move(glyph))
    {
        setComment(fcitx::Text(std::move(comment)));
    }

    void select(fcitx::InputContext *input_context) const override
    {
        picker_.commit(input_context, glyph_);
    }

private:
    EmojiPicker &picker_;
    std::string glyph_;
};

} // namespace

EmojiPickerState::EmojiPickerState()
{
    query.setMaxSize(emoji::max_query_bytes);
}

EmojiPicker::EmojiPicker(fcitx::Instance &instance,
                         std::filesystem::path history_path)
    : instance_(instance),
      state_factory_([](fcitx::InputContext &) {
          return new EmojiPickerState();
      }),
      history_(std::move(history_path))
{
    instance_.inputContextManager().registerProperty(
        "unilume-emoji-picker", &state_factory_);
}

bool EmojiPicker::available()
{
    return ensureData();
}

bool EmojiPicker::trigger(fcitx::InputContext *input_context)
{
    if (!input_context || !ensureData()) {
        return false;
    }
    EmojiPickerState *state = stateFor(input_context);
    state->active = true;
    state->query.clear();
    updateUi(input_context);
    return true;
}

bool EmojiPicker::handle(fcitx::KeyEvent &event)
{
    if (event.isRelease()) {
        return true;
    }
    fcitx::InputContext *input_context = event.inputContext();
    EmojiPickerState *state = stateFor(input_context);
    if (!state || !state->active) {
        return false;
    }
    const auto candidate_list =
        input_context->inputPanel().candidateList();
    if (candidate_list) {
        const int index = event.key().digitSelection();
        if (index >= 0) {
            if (index < candidate_list->size()) {
                candidate_list->candidate(index).select(input_context);
            }
            return true;
        }
        if (event.key().checkKeyList(
                instance_.globalConfig().defaultPrevPage())) {
            if (candidate_list->toPageable()->hasPrev()) {
                candidate_list->toPageable()->prev();
                input_context->updateUserInterface(
                    fcitx::UserInterfaceComponent::InputPanel);
            }
            return true;
        }
        if (event.key().checkKeyList(
                instance_.globalConfig().defaultNextPage())) {
            if (candidate_list->toPageable()->hasNext()) {
                candidate_list->toPageable()->next();
                input_context->updateUserInterface(
                    fcitx::UserInterfaceComponent::InputPanel);
            }
            return true;
        }
        if (event.key().checkKeyList(
                instance_.globalConfig().defaultPrevCandidate())) {
            candidate_list->toCursorMovable()->prevCandidate();
            input_context->updateUserInterface(
                fcitx::UserInterfaceComponent::InputPanel);
            return true;
        }
        if (event.key().checkKeyList(
                instance_.globalConfig().defaultNextCandidate())) {
            candidate_list->toCursorMovable()->nextCandidate();
            input_context->updateUserInterface(
                fcitx::UserInterfaceComponent::InputPanel);
            return true;
        }
        if ((event.key().check(FcitxKey_Return) ||
             event.key().check(FcitxKey_KP_Enter) ||
             event.key().check(FcitxKey_space)) &&
            !candidate_list->empty() &&
            candidate_list->cursorIndex() >= 0) {
            candidate_list->candidate(candidate_list->cursorIndex())
                .select(input_context);
            return true;
        }
    }
    if (event.key().check(FcitxKey_Escape)) {
        reset(input_context);
        return true;
    }
    if (event.key().check(FcitxKey_BackSpace)) {
        if (!state->query.backspace()) {
            reset(input_context);
        } else {
            updateUi(input_context);
        }
        return true;
    }
    if (event.key().isModifier() || event.key().hasModifier()) {
        reset(input_context);
        return false;
    }
    const auto composed = instance_.processComposeString(
        input_context, event.key().sym());
    if (!composed) {
        return true;
    }
    const bool typed = composed->empty()
                           ? state->query.type(
                                 fcitx::Key::keySymToUnicode(
                                     event.key().sym()))
                           : state->query.type(*composed);
    if (typed) {
        updateUi(input_context);
    }
    return true;
}

bool EmojiPicker::active(fcitx::InputContext *input_context) const
{
    const EmojiPickerState *state = stateFor(input_context);
    return state && state->active;
}

void EmojiPicker::reset(fcitx::InputContext *input_context)
{
    EmojiPickerState *state = stateFor(input_context);
    if (!state) {
        return;
    }
    state->active = false;
    state->query.clear();
    state->query.shrinkToFit();
    input_context->inputPanel().reset();
    input_context->updatePreedit();
    input_context->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
}

bool EmojiPicker::clearHistory()
{
    if (!history_loaded_) {
        const emoji::HistoryLoadResult loaded = history_.load();
        (void)loaded;
        history_loaded_ = true;
    }
    return history_.clear();
}

void EmojiPicker::commit(fcitx::InputContext *input_context,
                         const std::string &glyph)
{
    input_context->commitString(glyph);
    const bool recorded = history_.record(glyph);
    (void)recorded;
    reset(input_context);
}

bool EmojiPicker::ensureData()
{
    if (load_attempted_) {
        return module_ && index_;
    }
    load_attempted_ = true;
    module_ = instance_.addonManager().addon("emoji", true);
    if (!module_ ||
        !module_->call<fcitx::IEmoji::check>("vi", true)) {
        module_ = nullptr;
        return false;
    }
    if (!history_loaded_) {
        const emoji::HistoryLoadResult loaded = history_.load();
        (void)loaded;
        history_loaded_ = true;
    }
    auto index = std::make_unique<emoji::SearchIndex>();
    module_->call<fcitx::IEmoji::prefix>(
        "vi", "", true,
        [&index](const std::string &keyword,
                 const std::vector<std::string> &glyphs) {
            const bool added = index->add(keyword, glyphs);
            (void)added;
            return index->size() < emoji::max_index_entries;
        });
    if (index->size() == 0) {
        module_ = nullptr;
        return false;
    }
    index_ = std::move(index);
    return true;
}

void EmojiPicker::updateUi(fcitx::InputContext *input_context)
{
    EmojiPickerState *state = stateFor(input_context);
    input_context->inputPanel().reset();
    auto candidates = std::make_unique<fcitx::CommonCandidateList>();
    candidates->setPageSize(
        instance_.globalConfig().defaultPageSize());
    candidates->setSelectionKey(fcitx::KeyList(10));
    candidates->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
    if (state->query.empty()) {
        for (const std::string &glyph : history_.active().recent) {
            candidates->append<EmojiCandidateWord>(
                *this, glyph, _("Recently used"));
        }
    } else {
        for (const emoji::SearchResult &result :
             index_->search(state->query.userInput())) {
            candidates->append<EmojiCandidateWord>(
                *this, result.glyph, result.keyword);
        }
    }
    if (!candidates->empty()) {
        candidates->setGlobalCursorIndex(0);
    }
    input_context->inputPanel().setCandidateList(std::move(candidates));

    fcitx::Text query(state->query.userInput());
    query.setCursor(state->query.cursorByChar());
    input_context->inputPanel().setPreedit(query);
    input_context->inputPanel().setAuxUp(
        fcitx::Text(state->query.empty()
                        ? _("Emoji: type to search or choose a recent item")
                        : _("Emoji search")));
    input_context->updatePreedit();
    input_context->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
}

EmojiPickerState *EmojiPicker::stateFor(
    fcitx::InputContext *input_context) const
{
    return input_context
               ? input_context->propertyFor(&state_factory_)
               : nullptr;
}

} // namespace unilume::fcitx5
