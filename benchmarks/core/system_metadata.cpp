// SPDX-License-Identifier: GPL-2.0-or-later

#include "system_metadata.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace unilume::benchmark {
namespace {

std::string cpuModel()
{
    std::ifstream input{"/proc/cpuinfo"};
    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view prefix{"model name"};
        if (line.starts_with(prefix)) {
            const std::size_t separator = line.find(':');
            if (separator != std::string::npos) {
                return line.substr(separator + 2);
            }
        }
    }
    return "unknown";
}

std::string timestampUtc()
{
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

} // namespace

ReportMetadata collectReportMetadata()
{
    return {
        UNILUME_BENCHMARK_COMMIT,
        UNILUME_BENCHMARK_COMPILER,
        UNILUME_BENCHMARK_BUILD_TYPE,
        cpuModel(),
        timestampUtc(),
        "benchmark_scoped_c_cpp_interposition",
    };
}

} // namespace unilume::benchmark
