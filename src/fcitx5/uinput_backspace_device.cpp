// SPDX-License-Identifier: GPL-2.0-or-later

#include "uinput_backspace_device.h"

#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>

namespace unilume::fcitx5 {

UinputBackspaceDevice::UinputBackspaceDevice()
{
    const int descriptor = open(
        "/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0 ||
        ioctl(descriptor, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(descriptor, UI_SET_KEYBIT, KEY_BACKSPACE) < 0) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return;
    }

    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x554c;
    setup.id.product = 0x0001;
    constexpr char device_name[] = "UniLume Backspace";
    static_assert(sizeof(device_name) <= UINPUT_MAX_NAME_SIZE);
    std::memcpy(setup.name, device_name, sizeof(device_name));
    if (ioctl(descriptor, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(descriptor, UI_DEV_CREATE) < 0) {
        close(descriptor);
        return;
    }
    file_descriptor_ = descriptor;
}

UinputBackspaceDevice::~UinputBackspaceDevice()
{
    if (file_descriptor_ >= 0) {
        (void)ioctl(file_descriptor_, UI_DEV_DESTROY);
        close(file_descriptor_);
    }
}

bool UinputBackspaceDevice::available() const
{
    return file_descriptor_ >= 0;
}

UinputBatchWriteStatus
UinputBackspaceDevice::emitBackspaces(std::size_t count) const
{
    constexpr std::size_t maximum_backspaces = 129;
    if (!available() || count == 0 || count > maximum_backspaces) {
        return UinputBatchWriteStatus::no_events;
    }
    std::array<input_event, maximum_backspaces * 4> events{};
    for (std::size_t index = 0; index < count; ++index) {
        input_event *event = events.data() + index * 4;
        event[0].type = EV_KEY;
        event[0].code = KEY_BACKSPACE;
        event[0].value = 1;
        event[1].type = EV_SYN;
        event[1].code = SYN_REPORT;
        event[2].type = EV_KEY;
        event[2].code = KEY_BACKSPACE;
        event[2].value = 0;
        event[3].type = EV_SYN;
        event[3].code = SYN_REPORT;
    }

    const auto bytes = static_cast<ssize_t>(
        count * 4 * sizeof(input_event));
    const ssize_t written = write(
        file_descriptor_, events.data(), static_cast<std::size_t>(bytes));
    return classifyUinputBatchWrite(written,
                                   static_cast<std::size_t>(bytes));
}

} // namespace unilume::fcitx5
