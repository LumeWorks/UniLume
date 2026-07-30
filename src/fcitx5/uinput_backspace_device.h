// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace unilume::fcitx5 {

enum class UinputBatchWriteStatus : std::uint8_t {
    complete,
    no_events,
    partial,
};

[[nodiscard]] constexpr UinputBatchWriteStatus classifyUinputBatchWrite(
    std::ptrdiff_t written,
    std::size_t expected)
{
    if (written <= 0) {
        return UinputBatchWriteStatus::no_events;
    }
    return static_cast<std::size_t>(written) == expected
               ? UinputBatchWriteStatus::complete
               : UinputBatchWriteStatus::partial;
}

class UinputBackspaceDevice {
public:
    UinputBackspaceDevice();
    ~UinputBackspaceDevice();
    UinputBackspaceDevice(const UinputBackspaceDevice &) = delete;
    UinputBackspaceDevice &operator=(const UinputBackspaceDevice &) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] UinputBatchWriteStatus
    emitBackspaces(std::size_t count) const;

private:
    int file_descriptor_{-1};
};

} // namespace unilume::fcitx5
