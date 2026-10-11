#pragma once

// USB HID input for the ports with a USB host (RP2350-PiZero): boot-protocol
// keyboards and mice, and generic HID gamepads/joysticks read through their
// report descriptor. Board independent, so it is tested on the desktop.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "port.h"

namespace dsp {
namespace port {
namespace hid {

// Boot keyboard report: modifiers, reserved, 6 key usages.
void apply_keyboard_report(const uint8_t* report, size_t length, HostInput& in);

// Boot mouse report: buttons, dx, dy[, wheel]. Motion accumulates until the
// front end reads it (HostInput::mouse_dx/dy are cleared by the caller).
void apply_mouse_report(const uint8_t* report, size_t length, HostInput& in);

// Gamepad reports described by a HID report descriptor.
class Gamepad {
public:
    // Parses the descriptor; false when it has no usable stick, hat or button.
    bool parse(const uint8_t* descriptor, size_t length);
    // Updates the pad fields of `in` from one input report.
    void apply_report(const uint8_t* report, size_t length, HostInput& in) const;
    bool uses_report_ids() const { return uses_ids_; }

private:
    struct Field {
        uint8_t report_id = 0;
        uint32_t bit = 0;     // offset in the report, after the id byte
        uint8_t size = 0;     // bits
        int32_t min = 0, max = 0;
        uint16_t page = 0, usage = 0;
    };
    static int32_t read(const uint8_t* data, size_t length, const Field& f);
    std::vector<Field> fields_;
    bool uses_ids_ = false;
    int buttons_ = 0;  // highest button number
};

}  // namespace hid
}  // namespace port
}  // namespace dsp
