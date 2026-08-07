// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace unilume::fcitx5 {

class UinputBackspaceDevice {
public:
    UinputBackspaceDevice();
    ~UinputBackspaceDevice();
    UinputBackspaceDevice(const UinputBackspaceDevice &) = delete;
    UinputBackspaceDevice &operator=(const UinputBackspaceDevice &) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool emitBackspace() const;

private:
    int file_descriptor_{-1};
};

} // namespace unilume::fcitx5
