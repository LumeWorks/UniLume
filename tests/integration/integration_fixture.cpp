// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_fixture.h"

#include <stdexcept>

namespace unilume::integration::test {

IntegrationFixture::IntegrationFixture(BackendProfile profile)
    : backend_(profile), controller_(backend_)
{
}

void IntegrationFixture::type(std::string_view text)
{
    for (std::size_t offset = 0; offset < text.size();) {
        const std::size_t length = codePointLength(text.substr(offset));
        controller_.submit({
            core::KeyKind::text,
            text.substr(offset, length),
            false,
            false,
            false,
        });
        pump();
        offset += length;
    }
}

void IntegrationFixture::backspace()
{
    controller_.submit({core::KeyKind::backspace, {}, false, false, false});
    pump();
}

void IntegrationFixture::reset()
{
    controller_.submit({core::KeyKind::reset, {}, false, false, false});
}

void IntegrationFixture::focusChange()
{
    controller_.resetForFocus();
}

void IntegrationFixture::pump(std::size_t events)
{
    for (const BackendCompletion &completion : backend_.advance(events)) {
        controller_.complete(completion.sequence_id, completion.success);
    }
}

void IntegrationFixture::drain()
{
    // Deep bounded-delay profiles intentionally hold hundreds of keys while
    // exercising the fixed controller queue. Keep the harness ceiling finite
    // but above the worst qualified profile (400 events per replacement).
    for (std::size_t step = 0; step < 100000; ++step) {
        if (!backend_.hasPending() &&
            controller_.transactionState() ==
                core::TransactionState::idle &&
            controller_.metrics().queue_depth == 0) {
            return;
        }
        if (backend_.hasPending()) {
            pump();
        } else if (controller_.activeSequence() != 0) {
            controller_.timeout(controller_.activeSequence());
        }
    }
    if (controller_.activeSequence() != 0) {
        controller_.timeout(controller_.activeSequence());
    }
    if (backend_.hasPending() ||
        controller_.metrics().queue_depth != 0) {
        throw std::runtime_error("integration event queue did not drain");
    }
}

const std::string &IntegrationFixture::output() const
{
    return backend_.text();
}

const core::TransactionMetrics &IntegrationFixture::metrics() const
{
    return controller_.metrics();
}

DeterministicBackend &IntegrationFixture::backend()
{
    return backend_;
}

core::DirectCommitController &IntegrationFixture::controller()
{
    return controller_;
}

std::size_t IntegrationFixture::codePointLength(std::string_view text)
{
    if (text.empty()) {
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text.front());
    if (lead <= 0x7f) {
        return 1;
    }
    if (lead >= 0xc2 && lead <= 0xdf) {
        return 2;
    }
    if (lead >= 0xe0 && lead <= 0xef) {
        return 3;
    }
    if (lead >= 0xf0 && lead <= 0xf4) {
        return 4;
    }
    return 1;
}

} // namespace unilume::integration::test
