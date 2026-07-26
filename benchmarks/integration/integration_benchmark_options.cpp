// SPDX-License-Identifier: GPL-2.0-or-later

#include "integration_benchmark_options.h"

#include <stdexcept>
#include <string_view>

namespace unilume::integration_benchmark {
namespace {

std::string_view value(std::string_view argument, std::string_view name)
{
    const std::string prefix = std::string(name) + "=";
    if (!argument.starts_with(prefix)) {
        return {};
    }
    return argument.substr(prefix.size());
}

} // namespace

BenchmarkOptions parseOptions(int argc, char **argv)
{
    BenchmarkOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--smoke") {
            options.smoke = true;
            options.keys = 10000;
        } else if (const auto item = value(argument, "--keys");
                   !item.empty()) {
            options.keys = std::stoull(std::string(item));
        } else if (const auto item = value(argument, "--profile");
                   !item.empty()) {
            options.profile = item;
        } else if (const auto item = value(argument, "--format");
                   !item.empty()) {
            options.format = item;
        } else if (const auto item = value(argument, "--output");
                   !item.empty()) {
            options.output = item;
        } else if (const auto item = value(argument, "--typing");
                   !item.empty()) {
            if (item == "enabled") {
                options.typing_conveniences = true;
            } else if (item == "disabled") {
                options.typing_conveniences = false;
            } else {
                throw std::invalid_argument(
                    "--typing must be disabled or enabled");
            }
        } else {
            throw std::invalid_argument(
                "unknown integration benchmark option: " +
                std::string(argument));
        }
    }
    if (options.keys == 0) {
        throw std::invalid_argument("--keys must be greater than zero");
    }
    if (options.profile != "all" && options.profile != "immediate" &&
        options.profile != "delayed" && options.profile != "stale") {
        throw std::invalid_argument(
            "--profile must be all, immediate, delayed, or stale");
    }
    if (options.format != "human" && options.format != "json") {
        throw std::invalid_argument("--format must be human or json");
    }
    return options;
}

} // namespace unilume::integration_benchmark
