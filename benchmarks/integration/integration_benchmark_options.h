// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace unilume::integration_benchmark {

struct BenchmarkOptions {
    std::size_t keys{1000};
    std::string profile{"all"};
    std::string format{"human"};
    std::filesystem::path output;
    bool typing_conveniences{};
    bool smoke{};
};

BenchmarkOptions parseOptions(int argc, char **argv);

} // namespace unilume::integration_benchmark
