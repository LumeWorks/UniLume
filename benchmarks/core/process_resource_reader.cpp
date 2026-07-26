// SPDX-License-Identifier: GPL-2.0-or-later

#include "process_resource_reader.h"

#include <dirent.h>
#include <time.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace unilume::benchmark {
namespace {

std::size_t directoryEntryCount(const char *path)
{
    DIR *directory = opendir(path);
    if (directory == nullptr) {
        throw std::runtime_error(
            std::string{"could not inspect process resource directory: "} +
            path);
    }

    std::size_t count = 0;
    while (const dirent *entry = readdir(directory)) {
        const std::string_view name{entry->d_name};
        count += name != "." && name != "..";
    }
    closedir(directory);
    return count;
}

} // namespace

std::size_t openFileDescriptorCount()
{
    // The descriptor opened by opendir is visible in /proc/self/fd. It is
    // present in every sample, so subtract it from the process-owned count.
    const std::size_t count = directoryEntryCount("/proc/self/fd");
    return count == 0 ? 0 : count - 1;
}

std::size_t threadCount()
{
    return directoryEntryCount("/proc/self/task");
}

double processCpuSeconds()
{
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
        throw std::runtime_error("could not read process CPU clock");
    }
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1'000'000'000.0;
}

} // namespace unilume::benchmark
