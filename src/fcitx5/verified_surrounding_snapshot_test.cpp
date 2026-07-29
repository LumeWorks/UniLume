// SPDX-License-Identifier: GPL-2.0-or-later

#include "verified_surrounding_snapshot.h"
#include "replacement_transport_contract.h"

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

    ok &= expect(
        !replacementTransportIsAtomic("wayland_v2"),
        "split Wayland v2 transport accepted direct replacement");
    ok &= expect(
        !replacementTransportIsAtomic("wayland"),
        "split Wayland v1 transport accepted direct replacement");
    ok &= expect(
        replacementTransportIsAtomic("dbus"),
        "existing synchronous frontend rejected direct replacement");

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

    VerifiedSurroundingTicket ticket;
    ok &= expect(
        ticket.prepare(true, valid).allowsReplacement(),
        "ticket rejected a valid prepared snapshot");
    const auto reused = ticket.consume(true, valid, 2);
    ok &= expect(
        reused.has_value() && reused->allowsReplacement(),
        "ticket did not reuse the matching snapshot");
    ok &= expect(
        !ticket.consume(true, valid, 2).has_value(),
        "single-use ticket was reused across operations");

    (void)ticket.prepare(true, valid);
    valid.setText("toi", 3, 3);
    ok &= expect(
        !ticket.consume(true, valid, 2).has_value(),
        "ticket survived replacement of the surrounding snapshot");

    (void)ticket.prepare(true, valid);
    ok &= expect(
        !ticket.consume(true, valid, 4)->allowsReplacement(),
        "ticket accepted deletion beyond the prepared cursor");
    (void)ticket.prepare(true, valid);
    ok &= expect(
        !ticket.consume(true, valid, -1)->allowsReplacement(),
        "ticket accepted a negative deletion count");
    (void)ticket.prepare(true, valid);
    ticket.clear();
    ok &= expect(
        !ticket.consume(true, valid, 0).has_value(),
        "cleared ticket remained reusable");

    return ok ? 0 : 1;
}
