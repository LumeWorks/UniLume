// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace unilume::core {

struct TransactionMetrics {
    bool active_transaction{};
    std::size_t queue_depth{};
    std::size_t max_queue_depth{};
    std::uint64_t completed_transactions{};
    std::uint64_t aborted_transactions{};
    std::uint64_t stale_result_count{};
    std::uint64_t duplicate_prevention_count{};
    std::uint64_t reset_count{};
    std::uint64_t queue_overflow_count{};
    std::uint64_t uncertain_outcome_count{};
    std::uint64_t discarded_queued_input_count{};
    std::uint64_t fallback_failure_count{};
};

} // namespace unilume::core
