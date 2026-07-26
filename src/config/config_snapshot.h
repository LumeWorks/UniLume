// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace unilume::config {

inline constexpr std::uint32_t schema_version = 1;

enum class InputMethod { telex, vni, viqr };
enum class OutputCharset { utf8 };

struct Snapshot {
    std::uint32_t version{schema_version};
    InputMethod input_method{InputMethod::telex};
    OutputCharset output_charset{OutputCharset::utf8};
    bool spell_check{true};
    bool free_marking{true};
    bool modern_tone{false};
    bool auto_restore{true};
    bool macro_enabled{false};

    friend bool operator==(const Snapshot &, const Snapshot &) = default;
};

struct DecodeResult {
    Snapshot snapshot{};
    bool migrated{};
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] Snapshot defaults();
[[nodiscard]] DecodeResult decode(std::string_view text);
[[nodiscard]] std::string encode(const Snapshot &snapshot);
[[nodiscard]] std::string validate(const Snapshot &snapshot);

} // namespace unilume::config
