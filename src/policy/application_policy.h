// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace unilume::policy {

inline constexpr std::size_t max_rules = 4096;
inline constexpr std::size_t max_identity_bytes = 128;
inline constexpr std::size_t max_serialized_bytes = 1024 * 1024;

enum class ApplicationMode {
    adaptive,
    automatic,
    direct,
    safe_preedit,
    off,
};

[[nodiscard]] constexpr ApplicationMode normalizeMode(ApplicationMode mode)
{
    return mode;
}

// A legacy mode is one that was removed by the Issue #127 migration.
// `adaptive` and `off` are the only modes the v1 migration target emits;
// `automatic`, `direct` and `safe_preedit` are accepted on decode for
// migration and downgraded to `adaptive`, but they must never round-trip
// back to disk.
[[nodiscard]] constexpr bool isLegacyMode(ApplicationMode mode)
{
    return mode == ApplicationMode::automatic ||
           mode == ApplicationMode::direct ||
           mode == ApplicationMode::safe_preedit;
}

// Map any legacy mode to its migration target.  `adaptive` and `off`
// are returned unchanged; `automatic`, `direct` and `safe_preedit` are
// collapsed to `adaptive` per Issue #127.
[[nodiscard]] constexpr ApplicationMode migrateMode(ApplicationMode mode)
{
    return isLegacyMode(mode) ? ApplicationMode::adaptive : mode;
}

enum class MatchKind {
    exact,
    prefix,
};

enum class ResolutionSource {
    missing_identity,
    exact_rule,
    prefix_rule,
    default_rule,
};

struct Rule {
    MatchKind kind{MatchKind::exact};
    std::string pattern;
    ApplicationMode mode{ApplicationMode::adaptive};

    friend bool operator==(const Rule &, const Rule &) = default;
};

struct Table {
    ApplicationMode default_mode{ApplicationMode::adaptive};
    std::vector<Rule> exact_rules;
    std::vector<Rule> prefix_rules;

    friend bool operator==(const Table &, const Table &) = default;
};

struct Snapshot {
    std::shared_ptr<const Table> table{std::make_shared<Table>()};

    friend bool operator==(const Snapshot &left, const Snapshot &right)
    {
        if (static_cast<bool>(left.table) !=
            static_cast<bool>(right.table)) {
            return false;
        }
        return !left.table || *left.table == *right.table;
    }
};

struct DecodeResult {
    Snapshot snapshot;
    bool legacy_modes{};
    std::size_t line{};
    std::string field;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

struct Resolution {
    ApplicationMode mode{ApplicationMode::adaptive};
    ResolutionSource source{ResolutionSource::missing_identity};
    std::string_view pattern;
};

[[nodiscard]] std::string validate(const Snapshot &snapshot);
[[nodiscard]] DecodeResult decode(std::string_view text);
[[nodiscard]] std::string encode(const Snapshot &snapshot);
[[nodiscard]] Resolution resolve(const Snapshot &snapshot,
                                 std::string_view application_identity);
[[nodiscard]] std::string_view modeName(ApplicationMode mode);

} // namespace unilume::policy
