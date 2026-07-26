// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_model.h"
#include "utf8_validation.h"

#include <fcitx-module/emoji/emoji_public.h>

#include <fcitx/addonmanager.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        ++failures;
    }
}

} // namespace

int main()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() / "unilume-emoji-api-XXXXXX")
            .string();
    char *directory = mkdtemp(pattern.data());
    expect(directory != nullptr, "module fixture directory is available");
    if (directory) {
        {
            std::ofstream config(
                std::filesystem::path(directory) / "emoji.conf");
            config << "[Addon]\n"
                      "Name=Emoji\n"
                      "Type=SharedLibrary\n"
                      "Library=libemoji\n"
                      "Category=Module\n"
                      "Version=" UNILUME_TEST_FCITX_VERSION "\n"
                      "OnDemand=True\n\n"
                      "[Addon/Dependencies]\n"
                      "0=core:5.0.0\n";
        }
        {
            fcitx::AddonManager manager(directory);
            manager.registerDefaultLoader(nullptr);
            manager.load();
            fcitx::AddonInstance *module = manager.addon("emoji", true);
            expect(module != nullptr, "installed Fcitx emoji module loads");
            if (module) {
                expect(module->call<fcitx::IEmoji::check>("vi", true),
                       "Vietnamese Fcitx emoji dictionary loads");
                std::size_t keywords = 0;
                std::size_t glyphs = 0;
                bool valid = true;
                module->call<fcitx::IEmoji::prefix>(
                    "vi", "", true,
                    [&keywords, &glyphs, &valid](
                        const std::string &keyword,
                        const std::vector<std::string> &values) {
                        valid =
                            valid &&
                            unilume::core::isValidUtf8(keyword) &&
                            !values.empty();
                        ++keywords;
                        for (const std::string &glyph : values) {
                            valid =
                                valid &&
                                unilume::core::isValidUtf8(glyph);
                            ++glyphs;
                        }
                        return keywords < 32;
                    });
                expect(
                    valid && keywords > 0 && glyphs > 0,
                    "real module prefix API returns valid bounded fixtures");

                unilume::emoji::SearchIndex index;
                std::string fixture_query;
                module->call<fcitx::IEmoji::prefix>(
                    "vi", "", true,
                    [&index, &fixture_query](
                        const std::string &keyword,
                        const std::vector<std::string> &values) {
                        if (fixture_query.empty() &&
                            keyword.size() <=
                                unilume::emoji::max_query_bytes) {
                            fixture_query = keyword;
                        }
                        const bool added = index.add(keyword, values);
                        (void)added;
                        return index.size() <
                               unilume::emoji::max_index_entries;
                    });
                const auto started = std::chrono::steady_clock::now();
                bool found = true;
                for (int run = 0; run < 20; ++run) {
                    found = found &&
                            !index.search(fixture_query).empty();
                }
                const auto elapsed =
                    std::chrono::steady_clock::now() - started;
                expect(
                    found &&
                        elapsed < std::chrono::seconds(2),
                    "bounded real-data search remains responsive");
            }
        }
        std::filesystem::remove(
            std::filesystem::path(directory) / "emoji.conf");
        fcitx::AddonManager missing(directory);
        missing.registerDefaultLoader(nullptr);
        missing.load();
        expect(missing.addon("emoji", true) == nullptr,
               "missing emoji module is reported without failure");
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    return failures == 0 ? 0 : 1;
}
