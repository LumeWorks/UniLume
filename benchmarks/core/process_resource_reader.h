// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

namespace unilume::benchmark {

[[nodiscard]] std::size_t openFileDescriptorCount();
[[nodiscard]] std::size_t threadCount();
[[nodiscard]] double processCpuSeconds();

} // namespace unilume::benchmark
