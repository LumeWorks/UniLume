// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_session.h"

#include <stdexcept>

namespace unilume::integration_benchmark {

IntegrationSession::IntegrationSession(
    integration::test::BackendProfile profile,
    const core::TypingConvenienceOptions &typing_options)
    : backend_(profile), controller_(backend_)
{
    controller_.setTypingOptions(typing_options);
}

void IntegrationSession::submit(std::string_view key_text)
{
    controller_.submit(
        {core::KeyKind::text, key_text, false, false, false});
    pump();
}

void IntegrationSession::drain()
{
    for (std::size_t step = 0; step < 10000; ++step) {
        if (!backend_.hasPending() &&
            controller_.transactionState() ==
                core::TransactionState::idle &&
            controller_.metrics().queue_depth == 0) {
            return;
        }
        pump();
    }
    if (controller_.activeSequence() != 0) {
        controller_.timeout(controller_.activeSequence());
    }
    if (backend_.hasPending() ||
        controller_.metrics().queue_depth != 0) {
        throw std::runtime_error("integration benchmark queue did not drain");
    }
}

const std::string &IntegrationSession::output() const
{
    return backend_.text();
}

std::size_t IntegrationSession::appliedEvents() const
{
    return backend_.appliedEvents();
}

const core::TransactionMetrics &IntegrationSession::metrics() const
{
    return controller_.metrics();
}

void IntegrationSession::pump(std::size_t events)
{
    for (const auto &completion : backend_.advance(events)) {
        controller_.complete(completion.sequence_id, completion.success);
    }
}

} // namespace unilume::integration_benchmark
