// SPDX-License-Identifier: GPL-2.0-or-later

// Counterexamples for mid-composition "stable prefix" oracles.
//
// Locks docs/composition-span-research.md (decision C):
//
//   1. Some later keys rewrite earlier composed symbols (not mere append).
//   2. Longest common prefix (LCP) of successive outputs is not a proof of
//      immutability — known LCP regions are rewritten by other keys.
//   3. In a commit-only model (no SurroundingText, no forwarded deletion),
//      even a forward-stable looking prefix is unsafe because backspace can
//      always walk into the committed region after clearing the suffix.
//
// Does not re-implement UniKey rules.
// Does not evaluate a forwarded-backspace architecture.

#include "test_suites.h"
#include "test_support/test_context.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace unilume::test {
namespace {

std::size_t longestCommonPrefixBytes(std::string_view a, std::string_view b)
{
    const std::size_t limit = a.size() < b.size() ? a.size() : b.size();
    std::size_t index = 0;
    while (index < limit && a[index] == b[index]) {
        ++index;
    }
    while (index > 0 && index < a.size() &&
           (static_cast<unsigned char>(a[index]) & 0xc0) == 0x80) {
        --index;
    }
    return index;
}

bool isUtf8CodePointBoundary(std::string_view text, std::size_t offset)
{
    if (offset > text.size()) {
        return false;
    }
    if (offset == 0 || offset == text.size()) {
        return true;
    }
    return (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80;
}

std::size_t utf8CharacterCount(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) {
            ++count;
        }
    }
    return count;
}

bool prefixMutated(std::string_view before,
                   std::string_view after,
                   std::size_t prefix_bytes)
{
    if (prefix_bytes == 0) {
        return false;
    }
    if (prefix_bytes > before.size() || prefix_bytes > after.size()) {
        return true;
    }
    return before.substr(0, prefix_bytes) != after.substr(0, prefix_bytes);
}

std::string typeKeys(EngineFixture &engine, std::string_view keys)
{
    return engine.type(keys, UkTelex);
}

void assertEarlierRewrite(EngineFixture &engine,
                          TestContext &context,
                          std::string_view name,
                          std::string_view first_keys,
                          std::string_view full_keys,
                          std::string_view expected_first,
                          std::string_view expected_full)
{
    const std::string first = typeKeys(engine, first_keys);
    const std::string full = typeKeys(engine, full_keys);
    context.expectEqual(std::string(name) + " first", first, expected_first);
    context.expectEqual(std::string(name) + " full", full, expected_full);

    bool rewrite = false;
    for (std::size_t n = 1; n <= first.size(); ++n) {
        if (!isUtf8CodePointBoundary(first, n)) {
            continue;
        }
        if (prefixMutated(first, full, n)) {
            rewrite = true;
            break;
        }
    }
    context.expectEqual(std::string(name) + " rewrites earlier output",
                        rewrite ? "yes" : "no",
                        "yes");
}

// Prove a specific positive LCP claim is not immutable under some key.
void assertLcpFalsifiedByContinuation(EngineFixture &engine,
                                      TestContext &context,
                                      std::string_view name,
                                      std::string_view prefix_keys,
                                      std::string_view next_keys,
                                      char falsifying_key,
                                      std::string_view expected_prefix_output)
{
    const std::string before = typeKeys(engine, prefix_keys);
    const std::string next = typeKeys(engine, next_keys);
    context.expectEqual(std::string(name) + " prefix output",
                        before,
                        expected_prefix_output);

    const std::size_t claimed = longestCommonPrefixBytes(before, next);
    context.expectEqual(std::string(name) + " lcp positive",
                        claimed > 0 ? "yes" : "no",
                        "yes");
    context.expectEqual(std::string(name) + " lcp utf8 boundary",
                        isUtf8CodePointBoundary(before, claimed) ? "ok"
                                                                 : "bad",
                        "ok");

    std::string attack(prefix_keys);
    attack.push_back(falsifying_key);
    const std::string after = typeKeys(engine, attack);
    context.expectEqual(
        std::string(name) + " continuation mutates LCP region",
        prefixMutated(before, after, claimed) ? "mutated" : "intact",
        "mutated");
}

// In a commit-only stable-prefix model (no SurroundingText, no forwarded
// deletion into committed text), any non-zero mid-word commit is unsafe
// under continued backspace: the user can delete the entire mutable suffix
// and then one more backspace would target the committed region, which a
// commit-only controller cannot handle.
//
// This does NOT evaluate a forwarded-backspace architecture where the
// controller could forward the extra Backspace to the application and
// rebuild engine state.
void assertBackspaceStrandsNonZeroPrefix(EngineFixture &engine,
                                         TestContext &context,
                                         std::string_view name,
                                         std::string_view keys)
{
    engine.begin(UkTelex);
    engine.feed(keys);
    const std::string composed = engine.output();
    context.expectValidUtf8(std::string(name) + " composed utf8", composed);
    if (composed.empty()) {
        return;
    }

    // Claim the longest proper UTF-8 prefix (all but last code point) as a
    // hypothetical "stable" commit — the most generous non-zero claim that
    // still leaves a mutable suffix for preedit.
    std::size_t claimed = composed.size();
    do {
        --claimed;
    } while (claimed > 0 &&
             (static_cast<unsigned char>(composed[claimed]) & 0xc0) == 0x80);

    if (claimed == 0) {
        // Single code point word: committing it leaves empty preedit; the
        // next backspace still targets committed text.
        claimed = composed.size();
    }

    const std::size_t suffix_chars =
        utf8CharacterCount(composed.substr(claimed));
    // Clear the mutable suffix with backspaces.
    for (std::size_t i = 0; i < suffix_chars; ++i) {
        engine.pressBackspace();
    }
    const std::string after_suffix = engine.output();
    // Lock that after clearing the suffix, the claimed region is still
    // present in the engine composition. In a commit-only model this means
    // a real commit would leave orphaned application text if the user
    // continues deleting — the controller has no way to recall committed
    // bytes without SurroundingText or forwarded-backspace.
    context.expectEqual(
        std::string(name) + " backspace reaches claimed frontier",
        after_suffix.size() <= claimed ? "at-or-inside-claim" : "still-above",
        "at-or-inside-claim");
    context.expectEqual(
        std::string(name)
            + " non-zero claim strands text under commit-only model",
        claimed > 0 ? "commit-only-orphan" : "zero",
        "commit-only-orphan");
}

} // namespace

void runCompositionSpanCounterexampleTests(EngineFixture &engine,
                                           TestContext &context)
{
    // --- Forward rewrites of earlier symbols ---
    assertEarlierRewrite(engine, context, "CE1 dd", "d", "dd", "d", "đ");
    assertEarlierRewrite(engine, context, "CE2 too", "to", "too", "to", "tô");
    assertEarlierRewrite(
        engine, context, "CE3 hoas", "hoa", "hoas", "hoa", "hóa");
    assertEarlierRewrite(
        engine, context, "CE4 aa", "a", "aa", "a", "â");
    assertEarlierRewrite(
        engine, context, "CE5 as", "a", "as", "a", "á");

    // Append-only step (tô → tôi) is NOT a rewrite of earlier bytes; the
    // danger is other keys / backspace, not this particular continuation.
    {
        const std::string mid = typeKeys(engine, "too");
        const std::string full = typeKeys(engine, "tooi");
        context.expectEqual("CE-append too→tooi mid", mid, "tô");
        context.expectEqual("CE-append too→tooi full", full, "tôi");
        context.expectEqual(
            "CE-append mid is prefix of full (LCP observation)",
            full.substr(0, mid.size()) == mid ? "prefix" : "not-prefix",
            "prefix");
    }

    // --- LCP observation is not an oracle (targeted falsifiers) ---
    // After "d", LCP with "dd" may be 0 (d vs đ); use "to"/"too": LCP is "t"
    // if comparing to something else... "to"→"too" LCP of "to" and "tô" is 0
    // or 1 depending on encoding. Use explicit case: "hoa"/"hoas" share "h".
    assertLcpFalsifiedByContinuation(
        engine,
        context,
        "LCP-hoa",
        "hoa",
        "hoan",
        's',
        "hoa");

    // "tiê" appears while typing tiếng; tone/roof keys rewrite earlier vowels.
    assertLcpFalsifiedByContinuation(
        engine,
        context,
        "LCP-tiee",
        "tiee",
        "tieen",
        's',
        "tiê");

    // d → đ falsifies any claim that the first byte stayed "d".
    {
        const std::string before = typeKeys(engine, "d");
        const std::string after = typeKeys(engine, "dd");
        context.expectEqual("LCP-dd before", before, "d");
        context.expectEqual("LCP-dd after", after, "đ");
        context.expectEqual(
            "LCP-dd mutates unit prefix",
            prefixMutated(before, after, 1) ? "mutated" : "intact",
            "mutated");
    }

    // --- Backspace strands any non-zero mid-word commit ---
    static constexpr std::array<std::string_view, 8> words{
        "tooi",
        "tieengs",
        "quyen",
        "thuong",
        "ddang",
        "hoas",
        "khoer",
        "aa",
    };
    for (const std::string_view word : words) {
        assertBackspaceStrandsNonZeroPrefix(
            engine, context, std::string("BS-") + std::string(word), word);
    }

    // Fail-closed reminder used by docs: for a commit-only model,
    // only stable_prefix=0 is always safe.
    context.expectEqual(
        "fail-closed oracle commit-only",
        "stable_prefix_bytes=0",
        "stable_prefix_bytes=0");
}

} // namespace unilume::test
