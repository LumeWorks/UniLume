// SPDX-License-Identifier: GPL-2.0-or-later

// Isolated prototype for Issue #47. This file exercises the selected
// composition ownership contract and documents executable counterexamples for
// rejected alternatives. It is test-only and is not linked into the addon.

#include "preedit_fallback_controller.h"
#include "test_assertions.h"
#include "test_suites.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace unilume::integration::test {
namespace {

enum class CompositionOwner
{
    none,
    verified_direct,
    client_preedit,
};

struct FrontendSnapshot
{
    bool focused{};
    bool surrounding_text{};
    bool cursor_valid{};
    std::size_t cursor{};
    std::size_t anchor{};
};

class OwnershipPrototype
{
public:
    CompositionOwner begin(const FrontendSnapshot &snapshot)
    {
        if (owner_ != CompositionOwner::none) {
            return owner_;
        }
        if (!snapshot.focused) {
            return CompositionOwner::none;
        }
        ++generation_;
        owner_ = canReplace(snapshot) ? CompositionOwner::verified_direct
                                      : CompositionOwner::client_preedit;
        return owner_;
    }

    void invalidateDirect()
    {
        if (owner_ == CompositionOwner::verified_direct) {
            owner_ = CompositionOwner::none;
            ++reset_count_;
        }
    }

    void reset()
    {
        owner_ = CompositionOwner::none;
        ++reset_count_;
    }

    [[nodiscard]] CompositionOwner owner() const
    {
        return owner_;
    }

    [[nodiscard]] std::uint64_t generation() const
    {
        return generation_;
    }

    [[nodiscard]] std::uint64_t resetCount() const
    {
        return reset_count_;
    }

private:
    static bool canReplace(const FrontendSnapshot &snapshot)
    {
        return snapshot.surrounding_text && snapshot.cursor_valid &&
               snapshot.cursor == snapshot.anchor;
    }

    CompositionOwner owner_{CompositionOwner::none};
    std::uint64_t generation_{};
    std::uint64_t reset_count_{};
};

struct VisualUpdate
{
    std::uint64_t sequence{};
    std::string text;
};

class ClientPreeditPrototype
{
public:
    VisualUpdate submit(char key)
    {
        const core::KeyInput input{
            core::KeyKind::text, std::string_view{&key, 1}, false, false, false,
        };
        const core::PreeditAction action = controller_.submit(input);
        document_.append(action.commit_text);
        return {++next_sequence_, std::string(controller_.preedit())};
    }

    void deliverVisual(const VisualUpdate &update)
    {
        // Every update is a complete snapshot. A late snapshot must never
        // replace a newer one; gaps only affect transient rendering.
        if (update.sequence <= visible_sequence_) {
            return;
        }
        visible_sequence_ = update.sequence;
        visible_preedit_ = update.text;
    }

    void reset()
    {
        controller_.reset();
        visible_preedit_.clear();
        visible_sequence_ = next_sequence_;
    }

    [[nodiscard]] std::string output() const
    {
        return document_ + std::string(controller_.preedit());
    }

private:
    core::PreeditFallbackController controller_;
    std::string document_;
    std::string visible_preedit_;
    std::uint64_t next_sequence_{};
    std::uint64_t visible_sequence_{};
};

std::string repeatedInput(std::size_t keys)
{
    static constexpr std::string_view corpus{
        "tooi ddang gox tieengs Vieetj http://abc.com/a1 "
        "user@example.com value_2 "};
    std::string input;
    input.reserve(keys);
    while (input.size() < keys) {
        input.append(
            corpus.substr(0, std::min(corpus.size(), keys - input.size())));
    }
    return input;
}

std::string runClientPreedit(std::string_view input,
                             bool inject_visual_faults,
                             unsigned int interval_ms = 1,
                             bool wall_paced = false)
{
    ClientPreeditPrototype prototype;
    std::optional<VisualUpdate> delayed;
    std::uint64_t virtual_time_ms{};
    std::uint64_t next_drop_ms{97};
    std::uint64_t next_reorder_ms{131};
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < input.size(); ++index) {
        virtual_time_ms += interval_ms;
        if (wall_paced) {
            std::this_thread::sleep_until(
                started + std::chrono::milliseconds{virtual_time_ms});
        }
        VisualUpdate update = prototype.submit(input[index]);
        if (!inject_visual_faults) {
            prototype.deliverVisual(update);
            continue;
        }

        // A dropped visual update and a reordered complete snapshot must not
        // alter the engine state or committed document.
        if (virtual_time_ms >= next_drop_ms) {
            next_drop_ms += 97;
            continue;
        }
        if (virtual_time_ms >= next_reorder_ms) {
            next_reorder_ms += 131;
            delayed = std::move(update);
            continue;
        }
        prototype.deliverVisual(update);
        if (delayed) {
            prototype.deliverVisual(*delayed);
            delayed.reset();
        }
    }
    return prototype.output();
}

struct BlindEditor
{
    std::string text;
    std::size_t cursor{};

    void replaceBeforeCursor(std::size_t characters, std::string_view value)
    {
        const std::size_t begin = characters > cursor ? 0 : cursor - characters;
        text.replace(begin, cursor - begin, value);
        cursor = begin + value.size();
    }
};

struct SeatOwnerRegistry
{
    bool acquire()
    {
        if (owned) {
            return false;
        }
        owned = true;
        return true;
    }

    bool owned{};
};

std::uint64_t processStatusKiB(std::string_view target)
{
    std::ifstream status{"/proc/self/status"};
    std::string key;
    while (status >> key) {
        if (key == target) {
            std::uint64_t value{};
            std::string unit;
            status >> value >> unit;
            return unit == "kB" ? value : 0;
        }
        std::string remainder;
        std::getline(status, remainder);
    }
    return 0;
}

std::uint64_t currentRssKiB()
{
    return processStatusKiB("VmRSS:");
}

std::uint64_t maximumRssKiB()
{
    return processStatusKiB("VmHWM:");
}

std::chrono::seconds requestedSoakDuration()
{
    const char *value = std::getenv("UNILUME_PROTOTYPE_SOAK_SECONDS");
    if (value == nullptr || value[0] == '\0') {
        return std::chrono::seconds{0};
    }
    char *end = nullptr;
    const unsigned long seconds = std::strtoul(value, &end, 10);
    if (end == value || *end != '\0') {
        return std::chrono::seconds{0};
    }
    return std::chrono::seconds{seconds};
}

bool wallBurstEnabled()
{
    const char *value = std::getenv("UNILUME_PROTOTYPE_WALL_BURST");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

} // namespace

void runZeroPreeditArchitectureTests(Assertions &assertions)
{
    // The selected owner is immutable while a composition is active.
    {
        OwnershipPrototype owner;
        const FrontendSnapshot x11_browser{true, false, false, 0, 0};
        const FrontendSnapshot wayland_browser{true, true, true, 12, 12};
        const FrontendSnapshot selected_text{true, true, true, 12, 8};
        const FrontendSnapshot unfocused{false, true, true, 12, 12};

        assertions.truth("unsupported frontend selects client preedit",
                         owner.begin(x11_browser) ==
                             CompositionOwner::client_preedit);
        assertions.truth("owner cannot change during active composition",
                         owner.begin(wayland_browser) ==
                             CompositionOwner::client_preedit);

        owner.reset();
        assertions.truth("verified Wayland snapshot selects direct",
                         owner.begin(wayland_browser) ==
                             CompositionOwner::verified_direct);
        const std::uint64_t direct_generation = owner.generation();
        owner.invalidateDirect();
        assertions.truth("capability loss crosses reset barrier",
                         owner.owner() == CompositionOwner::none &&
                             owner.resetCount() == 2);
        assertions.truth("post-barrier fallback owns next composition",
                         owner.begin(x11_browser) ==
                             CompositionOwner::client_preedit);
        assertions.truth("owner generation changes at barrier",
                         owner.generation() == direct_generation + 1);

        owner.reset();
        assertions.truth("selection prevents blind replacement",
                         owner.begin(selected_text) ==
                             CompositionOwner::client_preedit);

        owner.reset();
        assertions.truth("focus loss prevents any composition owner",
                         owner.begin(unfocused) == CompositionOwner::none);
    }

    // Client preedit: visual update loss/reordering cannot mutate the engine
    // or document. Its zero-preedit counterexample is a crash: uncommitted
    // text is intentionally discarded instead of guessed into the document.
    {
        const std::string input = repeatedInput(10000);
        assertions.equal("client preedit tolerates visual faults",
                         runClientPreedit(input, true),
                         runClientPreedit(input, false));

        ClientPreeditPrototype crash;
        crash.deliverVisual(crash.submit('t'));
        crash.deliverVisual(crash.submit('o'));
        assertions.equal("preedit pending before crash", crash.output(), "to");
        crash.reset();
        assertions.equal("crash recovery never commits guessed text",
                         crash.output(), "");
        for (const char key : std::string_view{"ddang "}) {
            crash.deliverVisual(crash.submit(key));
        }
        assertions.equal("reconnect starts a clean composition",
                         crash.output(), "đang ");
    }

    // Blind direct replacement: a cursor move between observation and edit
    // corrupts the document. Verified surrounding/cursor/anchor is mandatory.
    {
        BlindEditor editor{"toX", 3};
        editor.replaceBeforeCursor(2, "tô");
        assertions.equal("blind direct cursor counterexample", editor.text,
                         "ttô");
    }

    // Server preedit: without an acknowledged sequence, a late update can
    // overwrite the newest state. Sleep/retry cannot establish this ordering.
    {
        std::string visible;
        const std::array updates{
            VisualUpdate{2, "tô"},
            VisualUpdate{1, "to"},
        };
        for (const VisualUpdate &update : updates) {
            visible = update.text;
        }
        assertions.equal("server preedit reorder counterexample", visible,
                         "to");
    }

    // Wayland input-method protocol is already owned by Fcitx. The protocol
    // can transport an atomic edit when surrounding text exists, but a second
    // addon/helper input-method object violates the one-owner-per-seat rule.
    {
        SeatOwnerRegistry seat;
        assertions.truth("Fcitx acquires Wayland seat", seat.acquire());
        assertions.truth("second Wayland input method is rejected",
                         !seat.acquire());
    }

    // uinput/raw key injection targets the currently focused surface, not the
    // surface observed when composition started.
    {
        BlindEditor first{"to", 2};
        BlindEditor second{"abc", 3};
        BlindEditor *focused = &first;
        focused = &second;
        focused->replaceBeforeCursor(2, "tô");
        assertions.equal("uinput focus counterexample leaves original stale",
                         first.text, "to");
        assertions.equal("uinput focus counterexample corrupts new target",
                         second.text, "atô");
    }

    // Deterministic 1/2/5 ms scheduler profiles at both required burst sizes.
    // The virtual interval changes event timing only; the synchronous
    // client-preedit owner must produce identical output.
    static constexpr std::array intervals_ms{1U, 2U, 5U};
    static constexpr std::array burst_sizes{1000U, 10000U};
    const bool wall_paced = wallBurstEnabled();
    for (const unsigned int interval : intervals_ms) {
        for (const unsigned int size : burst_sizes) {
            const std::string input = repeatedInput(size);
            const std::string expected = runClientPreedit(input, false);
            const std::string actual =
                runClientPreedit(input, true, interval, wall_paced);
            assertions.equal("faulted burst output at virtual interval", actual,
                             expected);
        }
    }
}

void runZeroPreeditSoakTests(Assertions &assertions)
{
    static constexpr std::string_view input{"tooi ddang gox tieengs Vieetj "};
    static constexpr std::string_view expected{"tôi đang gõ tiếng Việt "};

    const std::chrono::seconds duration = requestedSoakDuration();
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + duration;
    auto next_checkpoint = started + std::chrono::minutes{1};
    std::uint64_t iterations = 0;
    std::uint64_t maximum_rss = currentRssKiB();
    std::vector<std::uint64_t> checkpoints;
    checkpoints.reserve(32);
    checkpoints.push_back(maximum_rss);

    do {
        ClientPreeditPrototype prototype;
        for (const char key : input) {
            prototype.deliverVisual(prototype.submit(key));
        }
        assertions.equal("zero-preedit prototype soak output",
                         prototype.output(), expected);
        ++iterations;
        const auto now = std::chrono::steady_clock::now();
        if (duration.count() != 0 && now >= next_checkpoint) {
            const std::uint64_t rss = currentRssKiB();
            maximum_rss = std::max(maximum_rss, rss);
            if (checkpoints.size() < 32) {
                checkpoints.push_back(rss);
            }
            next_checkpoint += std::chrono::minutes{1};
        }
    } while (
        (duration.count() == 0 && iterations < 128) ||
        (duration.count() != 0 && std::chrono::steady_clock::now() < deadline));

    const std::uint64_t final_rss = currentRssKiB();
    maximum_rss =
        std::max({maximum_rss, final_rss, maximumRssKiB()});
    std::size_t increasing_steps = 0;
    for (std::size_t index = 1; index < checkpoints.size(); ++index) {
        if (checkpoints[index] > checkpoints[index - 1]) {
            ++increasing_steps;
        }
    }
    const bool material_growth =
        checkpoints.size() >= 2 &&
        checkpoints.back() > checkpoints.front() + 1024;
    const bool sustained_growth =
        checkpoints.size() >= 6 &&
        increasing_steps * 5 >= (checkpoints.size() - 1) * 4;
    assertions.truth("prototype soak has no material monotonic RSS growth",
                     !(sustained_growth && material_growth));

    if (duration.count() != 0) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started);
        std::cout << "zero-preedit prototype soak"
                  << " elapsed_seconds=" << elapsed.count()
                  << " iterations=" << iterations
                  << " final_rss_kib=" << final_rss
                  << " maximum_rss_kib=" << maximum_rss << '\n';
    }
}

} // namespace unilume::integration::test
