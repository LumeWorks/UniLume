// SPDX-License-Identifier: GPL-2.0-or-later

#include "fuzz_harness.h"

#include "application_policy.h"
#include "config_snapshot.h"
#include "direct_commit_controller.h"
#include "dictionary_contract.h"
#include "keymap_contract.h"
#include "macro_contract.h"
#include "deterministic_backend.h"
#include "typing_pipeline.h"
#include "utf8_validation.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace unilume::fuzz {
namespace {

using integration::test::BackendCompletion;
using integration::test::BackendProfile;
using integration::test::DeterministicBackend;

std::span<const std::uint8_t> bounded(std::span<const std::uint8_t> data)
{
    return data.first(std::min(data.size(), max_input_bytes));
}

std::string bytes(std::span<const std::uint8_t> data)
{
    return {reinterpret_cast<const char *>(data.data()), data.size()};
}

void append(std::ostringstream &out, const core::KeyResult &result)
{
    out << result.handled << ':' << result.delete_before_cursor << ':'
        << result.commit_text.size() << ':' << result.sequence_id << ';';
    out.write(result.commit_text.data(),
              static_cast<std::streamsize>(result.commit_text.size()));
}

bool metricsValid(const core::DirectCommitController &controller)
{
    const auto &metrics = controller.metrics();
    return metrics.queue_depth <= core::DirectCommitController::queue_capacity &&
           metrics.max_queue_depth <=
               core::DirectCommitController::queue_capacity;
}

void deliver(core::DirectCommitController &controller,
             const std::vector<BackendCompletion> &completions)
{
    for (const auto &completion : completions) {
        controller.complete(completion.sequence_id, completion.success);
    }
}

core::KeyInput keyInput(std::uint8_t value)
{
    static constexpr std::string_view alphabet =
        "aasddwweeoouy1234567890.,:/?[]{}_- ";
    const char &key = alphabet[value % alphabet.size()];
    return {core::KeyKind::text, std::string_view(&key, 1),
            (value & 0x40) != 0, (value & 0x80) != 0, false};
}

} // namespace

Outcome runEngine(std::span<const std::uint8_t> input)
{
    const auto data = bounded(input);
    core::TypingPipeline engine;
    std::ostringstream trace;
    bool valid = true;
    const std::size_t steps = std::min(data.size(), max_operations);
    for (std::size_t index = 0; index < steps; ++index) {
        const std::uint8_t operation = data[index];
        if (operation % 23 == 0) {
            engine.setTypingOptions({
                .auto_capitalize = (operation & 1) != 0,
                .double_space_to_period = (operation & 2) != 0,
                .double_hyphen_to_em_dash = (operation & 4) != 0,
                .w_shortcut = static_cast<core::ShortcutScope>(
                    (operation >> 3) % 4),
                .bracket_shortcut = static_cast<core::ShortcutScope>(
                    (operation >> 5) % 4),
            });
            trace << 'P';
        } else if (operation % 19 == 0) {
            engine.reset();
            trace << 'R';
        } else if (operation % 17 == 0) {
            engine.setInputMethod(
                static_cast<UlInputMethod>((operation / 17) % 3));
            trace << 'M';
        } else if (operation % 13 == 0) {
            const UlEngineOptions options{
                static_cast<int>((operation >> 0) & 1),
                static_cast<int>((operation >> 1) & 1),
                static_cast<int>((operation >> 2) & 1),
                static_cast<int>((operation >> 3) & 1)};
            engine.setOptions(options);
            trace << 'O';
        } else {
            const core::KeyInput key =
                operation % 11 == 0
                    ? core::KeyInput{core::KeyKind::backspace}
                    : keyInput(operation);
            const core::KeyResult result = engine.process(key);
            valid = valid && result.delete_before_cursor >= 0 &&
                    result.commit_text.size() <= 4096 &&
                    core::isValidUtf8(result.commit_text);
            append(trace, result);
        }
    }
    return {valid, trace.str()};
}

Outcome runParsers(std::span<const std::uint8_t> input)
{
    const std::string text = bytes(bounded(input));
    std::ostringstream trace;
    bool valid = true;

    const config::DecodeResult config_result = config::decode(text);
    trace << "c:" << config_result.ok() << ':' << config_result.migrated << ':'
          << config_result.error << ';';
    if (config_result.ok()) {
        const std::string encoded = config::encode(config_result.snapshot);
        const config::DecodeResult decoded = config::decode(encoded);
        valid = valid && !encoded.empty() && decoded.ok() &&
                decoded.snapshot == config_result.snapshot && !decoded.migrated;
    }

    const macro::DecodeResult macro_result = macro::decode(text);
    trace << "m:" << macro_result.ok() << ':' << macro_result.migrated << ':'
          << macro_result.error << ';';
    if (macro_result.ok()) {
        const std::string encoded = macro::encode(macro_result.snapshot);
        const macro::DecodeResult decoded = macro::decode(encoded);
        valid = valid && !encoded.empty() && decoded.ok() &&
                decoded.snapshot == macro_result.snapshot && !decoded.migrated;
    }

    const keymap::DecodeResult keymap_result = keymap::decode(text);
    trace << "k:" << keymap_result.ok() << ':' << keymap_result.line << ':'
          << keymap_result.field << ':' << keymap_result.error << ';';
    if (keymap_result.ok()) {
        const std::string encoded = keymap::encode(keymap_result.snapshot);
        const keymap::DecodeResult decoded = keymap::decode(encoded);
        valid = valid && !encoded.empty() && decoded.ok() &&
                decoded.snapshot == keymap_result.snapshot;
    }

    const dictionary::DecodeResult dictionary_result =
        dictionary::decode(text);
    trace << "d:" << dictionary_result.ok() << ':'
          << dictionary_result.line << ':' << dictionary_result.field << ':'
          << dictionary_result.error << ';';
    if (dictionary_result.ok()) {
        const std::string encoded =
            dictionary::encode(dictionary_result.snapshot);
        const dictionary::DecodeResult decoded =
            dictionary::decode(encoded);
        valid = valid && !encoded.empty() && decoded.ok() &&
                decoded.snapshot == dictionary_result.snapshot;
    }

    const policy::DecodeResult policy_result = policy::decode(text);
    trace << "p:" << policy_result.ok() << ':'
          << policy_result.line << ':' << policy_result.field << ':'
          << policy_result.error << ';';
    if (policy_result.ok()) {
        const std::string encoded = policy::encode(policy_result.snapshot);
        const policy::DecodeResult decoded = policy::decode(encoded);
        valid = valid && !encoded.empty() && decoded.ok() &&
                decoded.snapshot == policy_result.snapshot;
    }
    return {valid, trace.str()};
}

Outcome runTransactions(std::span<const std::uint8_t> input)
{
    const auto data = bounded(input);
    const std::size_t delay = data.empty() ? 0 : data.front() % 4;
    DeterministicBackend backend(BackendProfile{.delay_events = delay});
    core::DirectCommitController controller(backend);
    std::ostringstream trace;
    bool valid = true;

    const std::size_t steps = std::min(data.size(), max_operations);
    for (std::size_t index = 0; index < steps; ++index) {
        const std::uint8_t operation = data[index];
        switch (operation % 12) {
        case 0:
            controller.resetForFocus();
            trace << 'F';
            break;
        case 1:
            controller.submit({core::KeyKind::navigation});
            trace << 'N';
            break;
        case 2:
            controller.submit({core::KeyKind::backspace});
            trace << 'B';
            break;
        case 3:
            controller.setInputMethod(
                static_cast<UlInputMethod>((operation / 12) % 3));
            trace << 'M';
            break;
        case 4: {
            const UlEngineOptions options{
                static_cast<int>((operation >> 0) & 1),
                static_cast<int>((operation >> 1) & 1),
                static_cast<int>((operation >> 2) & 1),
                static_cast<int>((operation >> 3) & 1)};
            controller.setOptions(options);
            trace << 'O';
            break;
        }
        case 5:
            deliver(controller, backend.advance((operation / 12) % 4));
            trace << 'A';
            break;
        case 6:
            controller.complete(controller.activeSequence() + 1, true);
            trace << 'S';
            break;
        case 7:
            backend.cancel(controller.activeSequence());
            controller.complete(controller.activeSequence(), false);
            trace << 'X';
            break;
        case 8:
            controller.timeout(controller.activeSequence());
            trace << 'T';
            break;
        case 9: {
            macro::Snapshot snapshot;
            snapshot.enabled = true;
            snapshot.entries.push_back({"btw", "bằng tiếng Việt"});
            controller.setMacros(snapshot);
            trace << 'C';
            break;
        }
        default: {
            const auto key = keyInput(operation);
            const auto status = controller.submit(key);
            if (status == core::SubmissionStatus::passthrough ||
                status == core::SubmissionStatus::unhandled) {
                backend.forwardRaw(0, key.text);
            }
            trace << static_cast<int>(status);
            break;
        }
        }
        valid = valid && metricsValid(controller) &&
                core::isValidUtf8(backend.text()) &&
                backend.eventLog().size() == backend.appliedEvents();
    }
    controller.resetForFocus();
    const auto &metrics = controller.metrics();
    trace << '|' << backend.text() << '|' << metrics.completed_transactions
          << ':' << metrics.aborted_transactions << ':'
          << metrics.stale_result_count << ':'
          << metrics.duplicate_prevention_count << ':'
          << metrics.queue_overflow_count << ':'
          << metrics.uncertain_outcome_count << ':'
          << metrics.fallback_failure_count;
    DeterministicBackend model;
    std::uint64_t previous_sequence = 0;
    for (const auto &event : backend.eventLog()) {
        const platform::ReplacementStatus replayed =
            event.kind ==
                    integration::test::BackendEventKind::fallback_commit
                ? model.requestFallbackCommit(
                      event.sequence_id, event.commit_text)
                : event.kind ==
                        integration::test::BackendEventKind::raw_passthrough
                    ? (model.forwardRaw(
                           event.delete_before_cursor, event.commit_text)
                           ? platform::ReplacementStatus::completed
                           : platform::ReplacementStatus::failed)
                    : model.requestReplacement(
                          event.sequence_id,
                          event.delete_before_cursor,
                          event.commit_text);
        valid = valid &&
                (event.kind ==
                     integration::test::BackendEventKind::raw_passthrough ||
                 event.sequence_id >= previous_sequence) &&
                replayed == platform::ReplacementStatus::completed;
        if (event.sequence_id != 0) {
            previous_sequence = event.sequence_id;
        }
    }
    valid = valid && metricsValid(controller) && !backend.hasPending() &&
            model.text() == backend.text();
    return {valid, trace.str()};
}

bool knownTransactionFaultsDetected()
{
    DeterministicBackend backend(BackendProfile{.delay_events = 1});
    core::DirectCommitController controller(backend);
    const auto s1 = controller.submit({core::KeyKind::text, "a"});
    if (s1 == core::SubmissionStatus::passthrough ||
        s1 == core::SubmissionStatus::unhandled) {
        backend.forwardRaw(0, "a");
    }
    const auto s2 = controller.submit({core::KeyKind::text, "s"});
    if (s2 == core::SubmissionStatus::passthrough ||
        s2 == core::SubmissionStatus::unhandled) {
        backend.forwardRaw(0, "s");
    }
    const std::uint64_t active = controller.activeSequence();
    controller.complete(active + 1, true);
    controller.complete(active + 1, true);
    const auto &metrics = controller.metrics();
    return metrics.stale_result_count == 2 &&
           metrics.duplicate_prevention_count == 2 &&
           backend.text() == "a";
}

void requireValid(const Outcome &outcome)
{
    if (!outcome.valid) {
        std::abort();
    }
}

} // namespace unilume::fuzz
