#include "machine/taito_68705.h"

namespace dsp {

Taito68705::Taito68705(uint32_t clock, Type type)
    : cpu_(clock, M6805::Type::M68705), type_(type) {
    if (type_ == Type::Arkanoid) {
        cpu_.set_memory_handlers(
            [this](uint16_t a) { return cpu_read_arkanoid(a); },
            [this](uint16_t a, uint8_t v) { cpu_write_arkanoid(a, v); });
    } else {
        cpu_.set_memory_handlers(
            [this](uint16_t a) { return cpu_read(a); },
            [this](uint16_t a, uint8_t v) { cpu_write(a, v); });
    }
}

void Taito68705::reset() {
    cpu_.reset();
    port_a_in_ = port_a_out_ = 0;
    port_b_in_ = port_b_out_ = 0;
    port_c_in_ = port_c_out_ = 0;
    ddr_a_ = ddr_b_ = ddr_c_ = 0;
    from_main_ = from_mcu_ = 0;
    main_sent_ = mcu_sent_ = false;
    reset_held_ = false;
}

void Taito68705::set_reset(bool held) {
    reset_held_ = held;
    if (held) {
        cpu_.reset();
        main_sent_ = mcu_sent_ = false;
    }
}

void Taito68705::run(int cycles) {
    if (reset_held_) return;
    cpu_.run(cycles);
}

uint8_t Taito68705::read() {
    mcu_sent_ = false;
    return from_mcu_;
}

void Taito68705::write(uint8_t value) {
    from_main_ = value;
    main_sent_ = true;
    mcu_sent_ = false;
    cpu_.set_irq(IrqLine::Assert);
}

uint8_t Taito68705::read_port_c_status() const {
    // Standard: bit0 = main_sent, bit1 = !mcu_sent
    // TigerHeli: bit0 = !main_sent, bit1 = mcu_sent
    if (type_ == Type::TigerHeli) {
        return uint8_t((main_sent_ ? 0 : 1) | (mcu_sent_ ? 2 : 0));
    }
    return uint8_t((main_sent_ ? 1 : 0) | (mcu_sent_ ? 0 : 2));
}

// ---- Standard / TigerHeli memory map ----

uint8_t Taito68705::cpu_read(uint16_t address) {
    address &= 0x7ff;
    switch (address) {
        case 0:
            return uint8_t((port_a_out_ & ddr_a_) | (port_a_in_ & ~ddr_a_));
        case 1:
            return uint8_t((port_b_out_ & ddr_b_) | (port_b_in_ & ~ddr_b_));
        case 2: {
            port_c_in_ = read_port_c_status();
            return uint8_t((port_c_out_ & ddr_c_) | (port_c_in_ & ~ddr_c_));
        }
        default:
            return mem_[address];
    }
}

void Taito68705::cpu_write(uint16_t address, uint8_t value) {
    address &= 0x7ff;
    switch (address) {
        case 0:
            port_a_out_ = value;
            // TigerHeli: writing port A also latches to host
            if (type_ == Type::TigerHeli) {
                from_mcu_ = value;
                mcu_sent_ = true;
            }
            break;
        case 1: {
            // Falling edge on bit 1 (ddr_b): latch host data into port A
            if ((ddr_b_ & 0x02) && !(value & 0x02) && (port_b_out_ & 0x02)) {
                port_a_in_ = from_main_;
                if (main_sent_) {
                    cpu_.set_irq(IrqLine::Clear);
                    main_sent_ = false;
                }
            }
            // Rising edge on bit 2: send port A to host
            if ((ddr_b_ & 0x04) && (value & 0x04) && !(port_b_out_ & 0x04)) {
                from_mcu_ = port_a_out_;
                mcu_sent_ = true;
            }
            // Falling edge bit 3 / 4: misc callbacks (e.g. sound latch strobes)
            if ((ddr_b_ & 0x08) && !(value & 0x08) && (port_b_out_ & 0x08)) {
                if (misc_call_) misc_call_(0, port_a_out_);
            }
            if ((ddr_b_ & 0x10) && !(value & 0x10) && (port_b_out_ & 0x10)) {
                if (misc_call_) misc_call_(1, port_a_out_);
            }
            port_b_out_ = value;
            break;
        }
        case 2:
            port_c_out_ = value;
            break;
        case 4:
            ddr_a_ = value;
            break;
        case 5:
            ddr_b_ = value;
            break;
        case 6:
            ddr_c_ = value;
            break;
        default:
            // RAM $10-$7F only; $80-$7FF is ROM
            if (address >= 0x10 && address <= 0x7f) mem_[address] = value;
            break;
    }
}

// ---- Arkanoid memory map (status on port C) ----

uint8_t Taito68705::cpu_read_arkanoid(uint16_t address) {
    address &= 0x7ff;
    switch (address) {
        case 0:
            return uint8_t((port_a_out_ & ddr_a_) | (port_a_in_ & ~ddr_a_));
        case 1:
            return arkanoid_call_ ? arkanoid_call_() : uint8_t(0xff);
        case 2: {
            // bit0 = main_sent, bit1 = !mcu_sent
            port_c_in_ = uint8_t((main_sent_ ? 1 : 0) | (mcu_sent_ ? 0 : 2));
            return uint8_t((port_c_out_ & ddr_c_) | (port_c_in_ & ~ddr_c_));
        }
        default:
            return mem_[address];
    }
}

void Taito68705::cpu_write_arkanoid(uint16_t address, uint8_t value) {
    address &= 0x7ff;
    switch (address) {
        case 0:
            port_a_out_ = value;
            break;
        case 2: {
            // Falling edge bit 2 on port C: accept host data
            if ((ddr_c_ & 0x04) && !(value & 0x04) && (port_c_out_ & 0x04)) {
                port_a_in_ = from_main_;
                main_sent_ = false;
                cpu_.set_irq(IrqLine::Clear);
            }
            // Falling edge bit 3: send to host
            if ((ddr_c_ & 0x08) && !(value & 0x08) && (port_c_out_ & 0x08)) {
                from_mcu_ = port_a_out_;
                mcu_sent_ = true;
            }
            port_c_out_ = value;
            break;
        }
        case 4:
            ddr_a_ = value;
            break;
        case 6:
            ddr_c_ = value;
            break;
        default:
            if (address >= 0x10 && address <= 0x7f) mem_[address] = value;
            break;
    }
}

}  // namespace dsp
