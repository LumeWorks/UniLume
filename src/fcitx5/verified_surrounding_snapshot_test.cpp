// SPDX-License-Identifier: GPL-2.0-or-later

#include "verified_surrounding_snapshot.h"

#include <fcitx/surroundingtext.h>

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main()
{
    using namespace unilume::fcitx5;
    bool ok = true;

    fcitx::SurroundingText valid;
    valid.setText("tôi", 3, 3);
    ok &= expect(
        validateSurroundingSnapshot(true, valid, 2).allowsReplacement(),
        "valid collapsed UTF-8 snapshot rejected");
    ok &= expect(
        !validateSurroundingSnapshot(false, valid, 0).allowsReplacement(),
        "snapshot accepted without capability");
    ok &= expect(
        !validateSurroundingSnapshot(true, valid, 4).allowsReplacement(),
        "delete beyond cursor accepted");
    ok &= expect(
        !validateSurroundingSnapshot(true, valid, -1).allowsReplacement(),
        "negative delete accepted");

    fcitx::SurroundingText selected;
    selected.setText("text", 4, 2);
    ok &= expect(
        !validateSurroundingSnapshot(true, selected, 1)
             .allowsReplacement(),
        "non-collapsed selection accepted");

    fcitx::SurroundingText invalid;
    invalid.invalidate();
    ok &= expect(
        !validateSurroundingSnapshot(true, invalid, 0)
             .allowsReplacement(),
        "invalid surrounding snapshot accepted");

    std::string oversized(max_verified_surrounding_bytes + 1, 'a');
    fcitx::SurroundingText too_large;
    too_large.setText(
        oversized,
        static_cast<unsigned int>(oversized.size()),
        static_cast<unsigned int>(oversized.size()));
    ok &= expect(
        !validateSurroundingSnapshot(true, too_large, 0)
             .allowsReplacement(),
        "oversized surrounding snapshot accepted");

    return ok ? 0 : 1;
}
