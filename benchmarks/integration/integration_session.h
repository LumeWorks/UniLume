// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "deterministic_backend.h"
#include "direct_commit_controller.h"

#include <cstddef>
#include <string_view>

namespace unilume::integration_benchmark {

class IntegrationSession {
public:
    explicit IntegrationSession(
        integration::test::BackendProfile profile,
        const core::TypingConvenienceOptions &typing_options = {});

    void submit(std::string_view key_text);
    void drain();

    [[nodiscard]] const std::string &output() const;
    [[nodiscard]] std::size_t appliedEvents() const;
    [[nodiscard]] const core::TransactionMetrics &metrics() const;

private:
    void pump(std::size_t events = 1);

    integration::test::DeterministicBackend backend_;
    core::DirectCommitController controller_;
};

} // namespace unilume::integration_benchmark
