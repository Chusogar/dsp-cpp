// USB keyboard, mouse and gamepad on the PIO-USB port (TinyUSB host).
#include "usb_input.h"

#include <cstring>

#include "hardware/clocks.h"
#include "hid.h"
#include "pio_usb.h"
#include "tusb.h"

namespace hid = dsp::port::hid;

namespace {

dsp::port::HostInput g_state;

struct Device {
    bool used = false;
    uint8_t dev = 0, instance = 0, protocol = 0;
    hid::Gamepad pad;
    bool is_pad = false;
};
Device g_devices[CFG_TUH_HID];

Device* find(uint8_t dev, uint8_t instance) {
    for (Device& d : g_devices)
        if (d.used && d.dev == dev && d.instance == instance) return &d;
    return nullptr;
}

}  // namespace

void usb_input_init() {
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = DSP_USB_DP_PIN;
    // PIO0, state machines 0-2 and DMA channel 0 (claimed by PIO-USB):
    // PicoDVI uses PIO1 and claims its DMA channels afterwards.
    cfg.pio_tx_num = 0;
    cfg.pio_rx_num = 0;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
    tuh_init(BOARD_TUH_RHPORT);
}

void usb_input_task() { tuh_task(); }

void usb_input_read(dsp::port::HostInput& out) {
    out = g_state;
    g_state.mouse_dx = 0;
    g_state.mouse_dy = 0;
}

extern "C" {

void tuh_hid_mount_cb(uint8_t dev, uint8_t instance, uint8_t const* desc, uint16_t desc_len) {
    Device* slot = nullptr;
    for (Device& d : g_devices)
        if (!d.used) {
            slot = &d;
            break;
        }
    if (!slot) return;
    *slot = Device();
    slot->used = true;
    slot->dev = dev;
    slot->instance = instance;
    slot->protocol = tuh_hid_interface_protocol(dev, instance);
    if (slot->protocol == HID_ITF_PROTOCOL_NONE) slot->is_pad = slot->pad.parse(desc, desc_len);
    if (slot->protocol == HID_ITF_PROTOCOL_MOUSE) g_state.mouse_connected = true;
    tuh_hid_receive_report(dev, instance);
}

void tuh_hid_umount_cb(uint8_t dev, uint8_t instance) {
    if (Device* d = find(dev, instance)) {
        if (d->protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            g_state.keys.fill(false);
            g_state.left_alt = false;
        }
        if (d->is_pad) {
            g_state.pad_connected = false;
            g_state.pad_up = g_state.pad_down = g_state.pad_left = g_state.pad_right = false;
            g_state.pad_a = g_state.pad_b = g_state.pad_x = g_state.pad_y = false;
            g_state.pad_start = g_state.pad_select = false;
        }
        if (d->protocol == HID_ITF_PROTOCOL_MOUSE) g_state.mouse_connected = false;
        d->used = false;
    }
}

void tuh_hid_report_received_cb(uint8_t dev, uint8_t instance, uint8_t const* report, uint16_t len) {
    if (Device* d = find(dev, instance)) {
        if (d->protocol == HID_ITF_PROTOCOL_KEYBOARD) hid::apply_keyboard_report(report, len, g_state);
        else if (d->protocol == HID_ITF_PROTOCOL_MOUSE) hid::apply_mouse_report(report, len, g_state);
        else if (d->is_pad) d->pad.apply_report(report, len, g_state);
    }
    tuh_hid_receive_report(dev, instance);
}

}  // extern "C"
