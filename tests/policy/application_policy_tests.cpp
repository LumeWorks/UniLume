// SPDX-License-Identifier: GPL-2.0-or-later

#include "application_policy.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    try {
        using namespace unilume::policy;
        const DecodeResult decoded = decode(
            "unilume_app_policy_version=1\n"
            "default\tautomatic\n"
            "rule\torg.example.*\tsafe-preedit\n"
            "rule\torg.example.editor\tdirect\n"
            "rule\torg.example.special*\toff\n");
        require(decoded.ok(), "valid application policy rejected");
        require(decoded.legacy_modes,
                "legacy application modes were not reported");
        require(decode(encode(decoded.snapshot)).snapshot == decoded.snapshot,
                "application policy round-trip failed");

        // Issue #127 migration: automatic/direct/safe-preedit collapse to
        // adaptive; off stays off.
        require(decoded.snapshot.table->default_mode ==
                    ApplicationMode::adaptive,
                "default automatic was not migrated to adaptive");
        const Resolution exact =
            resolve(decoded.snapshot, "org.example.editor");
        require(exact.mode == ApplicationMode::adaptive &&
                    exact.source == ResolutionSource::exact_rule,
                "exact rule did not outrank prefix");
        const Resolution longest =
            resolve(decoded.snapshot, "org.example.special.child");
        require(longest.mode == ApplicationMode::off &&
                    longest.pattern == "org.example.special",
                "longest prefix rule did not win");
        require(resolve(decoded.snapshot, "org.example.other").mode ==
                    ApplicationMode::adaptive,
                "safe-preedit rule was migrated to adaptive");
        require(resolve(decoded.snapshot, "org.other").mode ==
                    ApplicationMode::adaptive,
                "automatic default was migrated to adaptive");
        const Resolution missing = resolve(decoded.snapshot, "");
        require(missing.mode == ApplicationMode::adaptive &&
                    missing.source == ResolutionSource::missing_identity,
                "missing identity did not select adaptive mode");
        // encode must emit only adaptive/off, never the legacy names.
        const std::string encoded = encode(decoded.snapshot);
        require(encoded.find("automatic") == std::string::npos &&
                    encoded.find("direct") == std::string::npos &&
                    encoded.find("safe-preedit") == std::string::npos,
                "legacy modes were not purged from the encoded output");
        require(encoded.find("adaptive") != std::string::npos,
                "adaptive mode was not serialized");

        require(!decode(
                    "unilume_app_policy_version=1\n"
                    "default\tautomatic\n"
                    "rule\torg.*.bad\tdirect\n")
                     .ok(),
                "middle wildcard accepted");
        require(!decode(
                    "unilume_app_policy_version=1\n"
                    "default\tautomatic\n"
                    "rule\tứng.dụng\tdirect\n")
                     .ok(),
                "non-ASCII application identity accepted");
        require(!decode(
                    "unilume_app_policy_version=1\n"
                    "default\tautomatic\n"
                    "rule\torg.app\tdirect\n"
                    "rule\torg.app\toff\n")
                     .ok(),
                "conflicting rule accepted");
        require(!decode(
                    "unilume_app_policy_version=1\n"
                    "rule\torg.app\tdirect\n")
                     .ok(),
                "missing default accepted");

        auto invalid_table = std::make_shared<Table>();
        invalid_table->exact_rules.push_back(
            {MatchKind::prefix, "org.app", ApplicationMode::direct});
        require(!validate({std::move(invalid_table)}).empty(),
                "rule kind inconsistent with canonical collection accepted");

        for (std::size_t index = 0; index < 500; ++index) {
            const std::string_view identity =
                index % 2 == 0 ? "org.example.editor"
                               : "org.example.other";
            // Both legacy modes (direct/safe-preedit) migrate to adaptive,
            // so the burst should resolve both identities to adaptive.
            require(resolve(decoded.snapshot, identity).mode ==
                        ApplicationMode::adaptive,
                    "focus burst produced a non-deterministic resolution");
        }

        // -- Issue #127 migration tests --

        // A pure adaptive/off config round-trips losslessly and does NOT
        // report legacy modes.
        {
            const DecodeResult pure = decode(
                "unilume_app_policy_version=1\n"
                "default\tadaptive\n"
                "rule\torg.app*\toff\n");
            require(pure.ok(), "pure adaptive/off policy rejected");
            require(!pure.legacy_modes,
                    "pure adaptive/off policy reported legacy modes");
            require(decode(encode(pure.snapshot)).snapshot ==
                        pure.snapshot,
                    "pure policy round-trip failed");
        }

        // Each legacy mode migrates to adaptive; off stays off.
        {
            for (const std::string_view legacy_mode :
                 {"automatic", "direct", "safe-preedit"}) {
                std::string config =
                    "unilume_app_policy_version=1\n";
                config += "default\t";
                config += legacy_mode;
                config += "\nrule\torg.app*\toff\n";
                const DecodeResult migrated = decode(config);
                require(migrated.ok(),
                        "legacy mode policy rejected");
                require(migrated.legacy_modes,
                        "legacy mode was not flagged");
                require(migrated.snapshot.table->default_mode ==
                            ApplicationMode::adaptive,
                        "legacy default was not migrated to adaptive");
                require(migrated.snapshot.table->prefix_rules[0].mode ==
                            ApplicationMode::off,
                        "off rule was modified during migration");
            }
        }

        // After migration, encode + re-decode should be stable and not
        // report legacy modes (the migrated output only uses adaptive/off).
        {
            const DecodeResult legacy = decode(
                "unilume_app_policy_version=1\n"
                "default\tautomatic\n"
                "rule\torg.editor\tdirect\n");
            require(legacy.ok() && legacy.legacy_modes, "legacy setup");
            const std::string migrated_text = encode(legacy.snapshot);
            require(migrated_text.find("automatic") ==
                        std::string::npos,
                    "encoded output still contains automatic");
            require(migrated_text.find("direct") == std::string::npos,
                    "encoded output still contains direct");
            const DecodeResult redecoded = decode(migrated_text);
            require(redecoded.ok(), "re-decoded migrated output rejected");
            require(!redecoded.legacy_modes,
                    "migrated output reported legacy modes");
            require(redecoded.snapshot == legacy.snapshot,
                    "migrated round-trip is not stable");
        }

        // A config that already uses adaptive alongside legacy modes: only
        // the legacy entries trigger migration; adaptive survives unchanged.
        {
            const DecodeResult mixed = decode(
                "unilume_app_policy_version=1\n"
                "default\tadaptive\n"
                "rule\torg.legacy\tdirect\n"
                "rule\torg.modern*\toff\n");
            require(mixed.ok(), "mixed policy rejected");
            require(mixed.legacy_modes, "mixed policy did not flag legacy");
            require(resolve(mixed.snapshot, "org.legacy").mode ==
                        ApplicationMode::adaptive,
                    "legacy direct rule was not migrated");
            require(resolve(mixed.snapshot, "org.modern.child").mode ==
                        ApplicationMode::off,
                    "off rule was modified");
        }

        // -- Corrupted config tests --
        require(!decode("").ok(), "empty config accepted");
        require(!decode("unilume_app_policy_version=2\n"
                        "default\tadaptive\n").ok(),
                "future version accepted");
        require(!decode("unilume_app_policy_version=1\n"
                        "default\tbogus\n").ok(),
                "unknown default mode accepted");
        require(!decode("unilume_app_policy_version=1\n"
                        "default\tadaptive\n"
                        "default\toff\n").ok(),
                "duplicate default accepted");
        require(!decode("unilume_app_policy_version=1\n"
                        "default\tadaptive\n"
                        "rule\torg.app\tbogus\n").ok(),
                "unknown rule mode accepted");
        require(!decode("unilume_app_policy_version=1\n"
                        "default\tadaptive\n"
                        "rule\torg.app\tautomatic\n"
                        "rule\torg.app\toff\n").ok(),
                "conflicting rules accepted");
        require(!decode(
                    "unilume_app_policy_version=1\n"
                    "default\tadaptive\n"
                    "rule\torg.app*\tadaptive*\n").ok(),
                "double wildcard accepted");
        require(!decode(
                    "unilume_app_policy_version=1\n"
                    "default\tadaptive\n"
                    "rule\t" +
                    std::string(200, 'a') + "\tadaptive\n").ok(),
                "oversized pattern accepted");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
