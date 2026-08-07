// SPDX-License-Identifier: GPL-2.0-or-later

#include "uinput_backspace_device.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

bool UinputBackspaceDevice::emitBackspace() const
{
    if (!available()) {
        return false;
    }
    input_event events[4]{};
    events[0].type = EV_KEY;
    events[0].code = KEY_BACKSPACE;
    events[0].value = 1;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[2].type = EV_KEY;
    events[2].code = KEY_BACKSPACE;
    events[2].value = 0;
    events[3].type = EV_SYN;
    events[3].code = SYN_REPORT;

    constexpr auto bytes = static_cast<ssize_t>(sizeof(events));
    ssize_t written;
    do {
        written = write(file_descriptor_, events,
                        static_cast<std::size_t>(bytes));
    } while (written < 0 && errno == EINTR);
    return written == bytes;
}

} // namespace unilume::fcitx5
