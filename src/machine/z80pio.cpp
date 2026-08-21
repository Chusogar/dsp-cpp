#include "machine/z80pio.h"

namespace dsp {

namespace {
constexpr uint8_t kIcwEnableInt = 0x80;
constexpr uint8_t kIcwMaskFollows = 0x10;
}  // namespace

void Z80Pio::reset() {
    reset_port(kPortA);
    reset_port(kPortB);
}

void Z80Pio::reset_port(int port) {
    Port& p = port_[port];
    set_mode(port, kModeInput);
    p.icw &= uint8_t(~kIcwEnableInt);
    p.ie = false;
    p.ip = false;
    p.ius = false;
    p.match = false;
    p.ior = 0;
    p.mask = 0xff;
    p.output = 0;
    set_rdy(port, false);
    p.next_control_word = kNextAny;
    p.input = 0;
    p.vector = 0;
}

void Z80Pio::set_port_handlers(int port, InHandler in, OutHandler out, ReadyHandler ready) {
    Port& p = port_[port & 1];
    p.in_func = std::move(in);
    p.out_func = std::move(out);
    p.ready_func = std::move(ready);
}

void Z80Pio::set_mode(int port, uint8_t mode) {
    Port& p = port_[port];
    switch (mode) {
        case kModeOutput:
            if (p.out_func) p.out_func(p.output);
            set_rdy(port, true);
            p.mode = kModeOutput;
            break;
        case kModeInput:
            p.mode = kModeInput;
            break;
        case kModeBidirectional:
            if (port != kPortB) p.mode = kModeBidirectional;
            break;
        case kModeBitControl:
            if (port == kPortA || port_[kPortA].mode != kModeBidirectional) set_rdy(port, false);
            p.ie = false;
            check_interrupts();
            p.match = false;
            p.next_control_word = kNextIor;
            p.mode = kModeBitControl;
            break;
        default:
            break;
    }
}

void Z80Pio::set_rdy(int port, bool state) {
    Port& p = port_[port];
    if (p.rdy != state) {
        p.rdy = state;
        if (p.ready_func) p.ready_func(state);
    }
}

void Z80Pio::check_interrupts() {
    bool asserted = false;
    for (int f = 0; f < 2; f++)
        if (interrupt_signalled(f)) asserted = true;
    if (interrupt_) interrupt_(asserted);
}

bool Z80Pio::interrupt_signalled(int port) {
    Port& p = port_[port];
    if (p.mode == kModeBitControl) {
        uint8_t data = uint8_t((p.input & p.ior) | (p.output & uint8_t(~p.ior)));
        uint8_t mask = uint8_t(~p.mask);
        data &= mask;
        bool match = false;
        const uint8_t sense = p.icw & 0x60;
        if (sense == 0x00 && data != mask) match = true;
        else if (sense == 0x20 && data != 0) match = true;
        else if (sense == 0x40 && data == 0) match = true;
        else if (sense == 0x60 && data == mask) match = true;
        if (!p.match && match) p.ip = true;
        p.match = match;
    }
    return p.ie && p.ip && !p.ius;
}

void Z80Pio::strobe(int port, bool state) {
    Port& p = port_[port];
    if (port_[kPortA].mode == kModeBidirectional) {
        if (p.rdy) {
            if (p.stb && !state) {  // falling edge
                if (port == kPortA) {
                    if (p.out_func) p.out_func(p.output);
                } else if (port_[kPortA].in_func) {
                    port_[kPortA].input = port_[kPortA].in_func();
                }
            } else if (!p.stb && state) {  // rising edge
                trigger_interrupt(port);
                set_rdy(port, false);
            }
        }
    } else {
        switch (p.mode) {
            case kModeOutput:
                if (p.rdy && !p.stb && state) {
                    trigger_interrupt(port);
                    set_rdy(port, false);
                }
                break;
            case kModeInput:
                if (!state) {
                    if (p.in_func) p.input = p.in_func();
                } else if (!p.stb && state) {
                    trigger_interrupt(port);
                    set_rdy(port, false);
                }
                break;
            default:
                break;
        }
    }
    p.stb = state;
}

void Z80Pio::trigger_interrupt(int port) {
    port_[port].ip = true;
    check_interrupts();
}

uint8_t Z80Pio::control_read() {
    return uint8_t((port_[kPortA].icw & 0xc0) | (port_[kPortB].icw >> 4));
}

void Z80Pio::control_write(int port, uint8_t value) {
    Port& p = port_[port];
    switch (p.next_control_word) {
        case kNextAny:
            if ((value & 1) == 0) {
                p.vector = value;
                p.icw |= kIcwEnableInt;
                p.ie = true;
                check_interrupts();
            } else {
                switch (value & 0x0f) {
                    case 0x0f: set_mode(port, uint8_t(value >> 6)); break;
                    case 0x07:
                        p.icw = value;
                        if ((p.icw & kIcwMaskFollows) != 0) {
                            p.ie = false;
                            p.ip = false;
                            check_interrupts();
                            p.match = false;
                            p.next_control_word = kNextMask;
                        }
                        break;
                    case 0x03:
                        p.icw = uint8_t((value & 0x80) | (p.icw & 0x7f));
                        p.ie = (p.icw & 0x80) != 0;
                        check_interrupts();
                        break;
                    default:
                        break;
                }
            }
            break;
        case kNextIor:
            p.ior = value;
            p.ie = (p.icw & 0x80) != 0;
            check_interrupts();
            p.next_control_word = kNextAny;
            break;
        case kNextMask:
            p.mask = value;
            p.ie = (p.icw & 0x80) != 0;
            check_interrupts();
            p.next_control_word = kNextAny;
            break;
        default:
            break;
    }
}

uint8_t Z80Pio::data_read(int port) {
    Port& p = port_[port];
    uint8_t data = 0;
    switch (p.mode) {
        case kModeOutput:
            data = p.output;
            break;
        case kModeInput:
            if (!p.stb && p.in_func) p.input = p.in_func();
            data = p.input;
            set_rdy(port, false);
            set_rdy(port, true);
            break;
        case kModeBidirectional:
            data = p.input;
            set_rdy(kPortB, false);
            set_rdy(kPortB, true);
            break;
        case kModeBitControl:
            if (p.in_func) p.input = p.in_func();
            data = uint8_t((p.input & p.ior) | (p.output & uint8_t(p.ior ^ 0xff)));
            break;
        default:
            break;
    }
    return data;
}

void Z80Pio::data_write(int port, uint8_t value) {
    Port& p = port_[port];
    switch (p.mode) {
        case kModeOutput:
            set_rdy(port, false);
            p.output = value;
            if (p.out_func) p.out_func(value);
            set_rdy(port, true);
            break;
        case kModeInput:
            p.output = value;
            break;
        case kModeBidirectional:
            set_rdy(port, false);
            p.output = value;
            if (!p.stb && p.out_func) p.out_func(value);
            set_rdy(port, true);
            break;
        case kModeBitControl:
            p.output = value;
            if (p.out_func) p.out_func(uint8_t(p.ior | (p.output & uint8_t(p.ior ^ 0xff))));
            break;
        default:
            break;
    }
}

uint8_t Z80Pio::read(uint16_t offset) {
    const int port = offset & 1;
    if ((offset & 2) != 0) return control_read();
    return data_read(port);
}

void Z80Pio::write(uint16_t offset, uint8_t value) {
    const int port = offset & 1;
    if ((offset & 2) != 0) control_write(port, value);
    else data_write(port, value);
}

}  // namespace dsp
