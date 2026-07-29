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
        require(decode(encode(decoded.snapshot)).snapshot == decoded.snapshot,
                "application policy round-trip failed");

        const Resolution exact =
            resolve(decoded.snapshot, "org.example.editor");
        require(exact.mode == ApplicationMode::direct &&
                    exact.source == ResolutionSource::exact_rule,
                "exact rule did not outrank prefix");
        const Resolution longest =
            resolve(decoded.snapshot, "org.example.special.child");
        require(longest.mode == ApplicationMode::off &&
                    longest.pattern == "org.example.special",
                "longest prefix rule did not win");
        require(resolve(decoded.snapshot, "org.example.other").mode ==
                    ApplicationMode::safe_preedit,
                "prefix rule did not match");
        require(resolve(decoded.snapshot, "org.other").mode ==
                    ApplicationMode::automatic,
                "default rule did not apply");
        const Resolution missing = resolve(decoded.snapshot, "");
        require(missing.mode == ApplicationMode::automatic &&
                    missing.source == ResolutionSource::missing_identity,
                "missing identity did not retain automatic mode");

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
            const ApplicationMode expected =
                index % 2 == 0 ? ApplicationMode::direct
                               : ApplicationMode::safe_preedit;
            require(resolve(decoded.snapshot, identity).mode == expected,
                    "focus burst produced a non-deterministic resolution");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
