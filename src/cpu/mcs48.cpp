#include "cpu/mcs48.h"

namespace dsp {
namespace {

constexpr uint8_t kCycles[256] = {
    1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,  // 00
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 10
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 20
    1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,  // 30
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 40
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 50
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 60
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 70
    2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,  // 80
    1, 1, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,  // 90
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // a0
    2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // b0
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // c0
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // d0
    1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,  // e0
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // f0
};

}  // namespace

Mcs48::Mcs48(uint32_t clock, Chip chip) : clock_(clock / 15), chip_(chip) {
    switch (chip_) {
        case Chip::I8035:
            rom_mask_ = 0;
            ram_mask_ = 0x3f;
            feature_mask_ = kMcs48Feature;
            break;
        case Chip::I8039:
            rom_mask_ = 0;
            ram_mask_ = 0x7f;
            feature_mask_ = kMcs48Feature;
            break;
        case Chip::N7751:
            rom_mask_ = 0x3ff;
            ram_mask_ = 0x3f;
            feature_mask_ = kMcs48Feature;
            break;
        case Chip::I8042:
            rom_mask_ = 0x7ff;
            ram_mask_ = 0x7f;
            feature_mask_ = kUpi41Feature;
            break;
    }
}

void Mcs48::set_io_handlers(PortInHandler in_port, PortOutHandler out_port) {
    in_port_ = std::move(in_port);
    out_port_ = std::move(out_port);
}

void Mcs48::set_external_handlers(ReadHandler read, WriteHandler write) {
    ext_in_ = std::move(read);
    ext_out_ = std::move(write);
}

void Mcs48::set_psw(uint8_t value) {
    psw_ = value;
    update_regptr();
}

void Mcs48::update_regptr() {
    bank_base_ = (psw_ & 0x10) != 0 ? 24 : 0;
}

uint8_t Mcs48::read_program(uint16_t address) const {
    const uint16_t mask = rom_mask_ != 0 ? rom_mask_ : uint16_t(kRomSize - 1);
    return rom_[address & mask];
}

uint8_t Mcs48::read_rom() {
    const uint8_t value = read_program(pc_);
    pc_ = uint16_t(((pc_ + 1) & 0x7ff) | (pc_ & 0x800));
    return value;
}

uint8_t Mcs48::read_byte(uint16_t address) const {
    return read_program(address);
}

void Mcs48::reset() {
    pc_ = 0;
    old_pc_ = 0;
    psw_ = 0;
    update_regptr();
    f1_ = false;
    a11_ = 0;
    a_ = 0;
    dbbo_ = 0xff;
    bus_w(0xff);
    p1_ = 0xff;
    p2_ = 0xff;
    port_w(1, p1_);
    port_w(2, p2_);
    tirq_enabled_ = false;
    xirq_enabled_ = false;
    timecount_enabled_ = 0;
    timer_flag_ = false;
    sts_ = 0;
    flags_enabled_ = false;
    dma_enabled_ = false;
    irq_in_progress_ = false;
    timer_overflow_ = false;
    irq_polled_ = false;
    irq_ = IrqLine::Clear;
    reset_ = IrqLine::Clear;
    timer_ = 0;
    prescaler_ = 0;
    t1_history_ = 0;
    dbbi_ = 0;
    if (chip_ == Chip::N7751) i8243_.reset();
}

uint8_t Mcs48::upi41_master_r(uint8_t address) {
    if ((address & 1) != 0) {
        return uint8_t((sts_ & 0xf3) | (f1_ ? 8 : 0) | ((psw_ & 0x20) ? 4 : 0));
    }
    if ((sts_ & kStsObf) != 0) {
        sts_ = uint8_t(sts_ & ~kStsObf);
        if (flags_enabled_) {
            p2_ = uint8_t(p2_ & ~kP2Obf);
            port_w(2, p2_);
        }
    }
    return dbbo_;
}

void Mcs48::upi41_master_w(uint8_t address, uint8_t value) {
    dbbi_ = value;
    if ((sts_ & kStsIbf) == 0) {
        sts_ = uint8_t(sts_ | kStsIbf);
        if (flags_enabled_) {
            p2_ = uint8_t(p2_ & ~kP2Nibf);
            port_w(2, p2_);
        }
    }
    f1_ = (address & 1) != 0;
}

uint8_t Mcs48::test_r(uint8_t which) const {
    return in_port_ ? in_port_(uint16_t(MCS48_PORT_T0 + which)) : uint8_t(0xff);
}

uint8_t Mcs48::bus_r() const {
    return in_port_ ? in_port_(MCS48_PORT_BUS) : uint8_t(0xff);
}

void Mcs48::bus_w(uint8_t value) {
    if (out_port_) out_port_(MCS48_PORT_BUS, value);
}

uint8_t Mcs48::port_r(uint8_t port) const {
    return in_port_ ? in_port_(uint16_t(MCS48_PORT_P0 + port)) : uint8_t(0xff);
}

void Mcs48::port_w(uint8_t port, uint8_t value) {
    if (out_port_) out_port_(uint16_t(MCS48_PORT_P0 + port), value);
}

void Mcs48::expander_operation(int operation, uint8_t port) {
    p2_ = uint8_t((p2_ & 0xf0) | (operation << 2) | (port & 3));
    port_w(2, p2_);
    if (out_port_) out_port_(MCS48_PORT_PROG, 0);
    if (operation != MCS48_EXPANDER_OP_READ) {
        p2_ = uint8_t((p2_ & 0xf0) | (a_ & 0x0f));
        port_w(2, p2_);
    } else {
        p2_ = uint8_t(p2_ | 0x0f);
        port_w(2, p2_);
        a_ = uint8_t(port_r(2) & 0x0f);
    }
    if (out_port_) out_port_(MCS48_PORT_PROG, 1);
}

void Mcs48::push_pc_psw() {
    const uint8_t flags = psw();
    const uint8_t sp = uint8_t(flags & 0x07);
    ram_[8 + 2 * sp] = uint8_t(pc_);
    ram_[9 + 2 * sp] = uint8_t(((pc_ >> 8) & 0x0f) | (flags & 0xf0));
    set_psw(uint8_t((flags & 0xf0) | ((sp + 1) & 0x07)));
}

int Mcs48::check_irqs() {
    if (irq_in_progress_) return 0;
    if ((irq_ != IrqLine::Clear || (sts_ & kStsIbf) != 0) && xirq_enabled_) {
        if (irq_polled_) {
            pc_ = uint16_t(((old_pc_ + 1) & 0x7ff) | (old_pc_ & 0x800));
            cond(true);
        }
        push_pc_psw();
        pc_ = 0x03;
        irq_in_progress_ = true;
        return 2;
    }
    if (timer_overflow_ && tirq_enabled_) {
        push_pc_psw();
        pc_ = 0x07;
        irq_in_progress_ = true;
        timer_overflow_ = false;
        return 2;
    }
    return 0;
}

void Mcs48::burn_cycles(uint8_t count) {
    bool timerover = false;
    if (timecount_enabled_ == kTimerEnabled) {
        const uint8_t oldtimer = timer_;
        prescaler_ = uint8_t(prescaler_ + count);
        timer_ = uint8_t(timer_ + (prescaler_ >> 5));
        prescaler_ = uint8_t(prescaler_ & 0x1f);
        timerover = oldtimer != 0 && timer_ == 0;
    } else if (timecount_enabled_ == kCounterEnabled) {
        for (uint8_t i = 0; i < count; i++) {
            t1_history_ = uint8_t((t1_history_ << 1) | (test_r(1) & 1));
            if ((t1_history_ & 3) == 2) {
                timer_ = uint8_t(timer_ + 1);
                timerover = timer_ == 0;
            }
        }
    }
    if (timerover) {
        timer_flag_ = true;
        if (tirq_enabled_) timer_overflow_ = true;
    }
}

uint8_t Mcs48::p2_mask() const {
    uint8_t res = 0xff;
    if ((feature_mask_ & kUpi41Feature) == 0) return res;
    if (flags_enabled_) res = uint8_t(res & ~(kP2Obf | kP2Nibf));
    if (dma_enabled_) res = uint8_t(res & ~(kP2Drq | kP2Ndack));
    return res;
}

void Mcs48::add(uint8_t value) {
    const uint16_t result = uint16_t(a_ + value);
    const uint16_t nibbles = uint16_t((a_ & 0x0f) + (value & 0x0f));
    psw_ = uint8_t((psw_ & 0x3f) | ((nibbles & 0x10) ? 0x40 : 0) | ((result & 0x100) ? 0x80 : 0));
    a_ = uint8_t(result);
}

void Mcs48::addc(uint8_t value) {
    const uint8_t carry = uint8_t((psw_ & 0x80) != 0);
    const uint16_t result = uint16_t(a_ + value + carry);
    const uint16_t nibbles = uint16_t((a_ & 0x0f) + (value & 0x0f) + carry);
    psw_ = uint8_t((psw_ & 0x3f) | ((nibbles & 0x10) ? 0x40 : 0) | ((result & 0x100) ? 0x80 : 0));
    a_ = uint8_t(result);
}

void Mcs48::cond(bool test) {
    const uint16_t dest = uint16_t((pc_ & 0xf00) | read_rom());
    if (test) pc_ = dest;
}

int Mcs48::run(int cycles) {
    int executed = 0;
    update_regptr();
    while (executed < cycles) {
        if (reset_ != IrqLine::Clear) {
            const IrqLine request = reset_;
            reset();
            if (request == IrqLine::Assert) reset_ = IrqLine::Assert;
            executed = cycles;
            break;
        }

        int extra = check_irqs();
        old_pc_ = pc_;
        const uint8_t op = read_rom();

        switch (op) {
            case 0x00:
                break;
            case 0x02:
                if (chip_ == Chip::I8042) {
                    dbbo_ = a_;
                    sts_ = uint8_t(sts_ | kStsObf);
                    if (flags_enabled_ && (p2_ & kP2Obf) == 0) {
                        p2_ = uint8_t(p2_ | kP2Obf);
                        port_w(2, p2_);
                    }
                } else {
                    bus_w(a_);
                }
                break;
            case 0x03:
                add(read_rom());
                break;
            case 0x04:
            case 0x24:
            case 0x44:
            case 0x64:
            case 0x84:
            case 0xa4:
            case 0xc4:
            case 0xe4: {
                const uint8_t offset = read_rom();
                const uint16_t page = irq_in_progress_ ? uint16_t(0) : a11_;
                pc_ = uint16_t(offset | page | ((op >> 5) << 8));
                break;
            }
            case 0x05:
                xirq_enabled_ = true;
                break;
            case 0x07:
                a_ = uint8_t(a_ - 1);
                break;
            case 0x08:
                if (chip_ != Chip::I8042) a_ = bus_r();
                break;
            case 0x09:
                a_ = uint8_t(port_r(1) & p1_);
                break;
            case 0x0a:
                a_ = uint8_t(port_r(2) & p2_);
                break;
            case 0x0c:
            case 0x0d:
            case 0x0e:
            case 0x0f:
                expander_operation(MCS48_EXPANDER_OP_READ, uint8_t(4 + (op & 3)));
                break;
            case 0x10:
            case 0x11:
                set_ram_indir(op & 1, uint8_t(ram_indir(op & 1) + 1));
                break;
            case 0x12:
                cond((a_ & 1) != 0);
                break;
            case 0x13:
                addc(read_rom());
                break;
            case 0x14:
            case 0x34:
            case 0x54:
            case 0x74:
            case 0x94:
            case 0xb4:
            case 0xd4:
            case 0xf4: {
                const uint8_t offset = read_rom();
                push_pc_psw();
                const uint16_t page = irq_in_progress_ ? uint16_t(0) : a11_;
                pc_ = uint16_t(offset | page | ((op >> 5) << 8));
                break;
            }
            case 0x15:
                xirq_enabled_ = false;
                break;
            case 0x16:
                cond(timer_flag_);
                timer_flag_ = false;
                break;
            case 0x17:
                a_ = uint8_t(a_ + 1);
                break;
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
                set_r(op & 7, uint8_t(r(op & 7) + 1));
                break;
            case 0x20:
            case 0x21: {
                const uint8_t temp = a_;
                a_ = ram_indir(op & 1);
                set_ram_indir(op & 1, temp);
                break;
            }
            case 0x22:
                if (chip_ == Chip::I8042) {
                    sts_ = uint8_t(sts_ & ~kStsIbf);
                    if (flags_enabled_ && (p2_ & kP2Nibf) == 0) {
                        p2_ = uint8_t(p2_ | kP2Nibf);
                        port_w(2, p2_);
                    }
                    a_ = dbbi_;
                }
                break;
            case 0x23:
                a_ = read_rom();
                break;
            case 0x25:
                tirq_enabled_ = true;
                break;
            case 0x26:
                cond(test_r(0) == 0);
                break;
            case 0x27:
                a_ = 0;
                break;
            case 0x28:
            case 0x29:
            case 0x2a:
            case 0x2b:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f: {
                const uint8_t temp = a_;
                a_ = r(op & 7);
                set_r(op & 7, temp);
                break;
            }
            case 0x30:
            case 0x31: {
                const uint8_t mem = ram_indir(op & 1);
                const uint8_t acc = a_;
                a_ = uint8_t((acc & 0xf0) | (mem & 0x0f));
                set_ram_indir(op & 1, uint8_t((mem & 0xf0) | (acc & 0x0f)));
                break;
            }
            case 0x32:
                cond((a_ & 2) != 0);
                break;
            case 0x35:
                tirq_enabled_ = false;
                timer_overflow_ = false;
                break;
            case 0x36:
                cond(test_r(0) != 0);
                break;
            case 0x37:
                a_ = uint8_t(a_ ^ 0xff);
                break;
            case 0x39:
                p1_ = a_;
                port_w(1, p1_);
                break;
            case 0x3a: {
                const uint8_t mask = p2_mask();
                p2_ = uint8_t((p2_ & ~mask) | (a_ & mask));
                port_w(2, p2_);
                break;
            }
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f:
                expander_operation(MCS48_EXPANDER_OP_WRITE, uint8_t(4 + (op & 3)));
                break;
            case 0x40:
            case 0x41:
                a_ = uint8_t(a_ | ram_indir(op & 1));
                break;
            case 0x42:
                a_ = timer_;
                break;
            case 0x43:
                a_ = uint8_t(a_ | read_rom());
                break;
            case 0x45:
                if (timecount_enabled_ != kCounterEnabled) t1_history_ = test_r(1);
                timecount_enabled_ = kCounterEnabled;
                break;
            case 0x46:
                cond(test_r(1) == 0);
                break;
            case 0x47:
                a_ = uint8_t((a_ << 4) | (a_ >> 4));
                break;
            case 0x48:
            case 0x49:
            case 0x4a:
            case 0x4b:
            case 0x4c:
            case 0x4d:
            case 0x4e:
            case 0x4f:
                a_ = uint8_t(a_ | r(op & 7));
                break;
            case 0x50:
            case 0x51:
                a_ = uint8_t(a_ & ram_indir(op & 1));
                break;
            case 0x52:
                cond((a_ & 4) != 0);
                break;
            case 0x53:
                a_ = uint8_t(a_ & read_rom());
                break;
            case 0x55:
                timecount_enabled_ = kTimerEnabled;
                prescaler_ = 0;
                break;
            case 0x56:
                cond(test_r(1) != 0);
                break;
            case 0x57:
                if ((a_ & 0x0f) > 0x09 || (psw_ & 0x40) != 0) {
                    if (a_ > 0xf9) psw_ = uint8_t(psw_ | 0x80);
                    a_ = uint8_t(a_ + 0x06);
                }
                if ((a_ & 0xf0) > 0x90 || (psw_ & 0x80) != 0) {
                    a_ = uint8_t(a_ + 0x60);
                    psw_ = uint8_t(psw_ | 0x80);
                }
                break;
            case 0x58:
            case 0x59:
            case 0x5a:
            case 0x5b:
            case 0x5c:
            case 0x5d:
            case 0x5e:
            case 0x5f:
                a_ = uint8_t(a_ & r(op & 7));
                break;
            case 0x60:
            case 0x61:
                add(ram_indir(op & 1));
                break;
            case 0x62:
                timer_ = a_;
                break;
            case 0x65:
                timecount_enabled_ = 0;
                break;
            case 0x67: {
                const uint8_t temp = a_;
                a_ = uint8_t((a_ >> 1) | ((psw_ & 0x80) != 0 ? 0x80 : 0));
                psw_ = uint8_t((psw_ & 0x7f) | ((temp & 1) != 0 ? 0x80 : 0));
                break;
            }
            case 0x68:
            case 0x69:
            case 0x6a:
            case 0x6b:
            case 0x6c:
            case 0x6d:
            case 0x6e:
            case 0x6f:
                add(r(op & 7));
                break;
            case 0x70:
            case 0x71:
                addc(ram_indir(op & 1));
                break;
            case 0x72:
                cond((a_ & 8) != 0);
                break;
            case 0x75:
                break;
            case 0x76:
                cond(f1_);
                break;
            case 0x77:
                a_ = uint8_t((a_ >> 1) | (a_ << 7));
                break;
            case 0x78:
            case 0x79:
            case 0x7a:
            case 0x7b:
            case 0x7c:
            case 0x7d:
            case 0x7e:
            case 0x7f:
                addc(r(op & 7));
                break;
            case 0x80:
                if (chip_ != Chip::I8042) a_ = ext_in_ ? ext_in_(r(0)) : uint8_t(0xff);
                break;
            case 0x81:
                if (chip_ != Chip::I8042) a_ = ext_in_ ? ext_in_(r(1)) : uint8_t(0xff);
                break;
            case 0x83: {
                const uint8_t sp = uint8_t((psw() - 1) & 0x07);
                pc_ = ram_[8 + 2 * sp];
                pc_ = uint16_t(pc_ | (uint16_t(ram_[9 + 2 * sp]) << 8));
                pc_ = uint16_t(pc_ & (irq_in_progress_ ? 0x7ff : 0xfff));
                set_psw(uint8_t((psw() & 0xf0) | sp));
                break;
            }
            case 0x85:
                psw_ = uint8_t(psw_ & ~0x20);
                break;
            case 0x86:
                if (chip_ == Chip::I8042) {
                    cond((sts_ & kStsObf) != 0);
                } else {
                    irq_polled_ = irq_ != IrqLine::Clear;
                    cond(irq_ != IrqLine::Clear);
                }
                break;
            case 0x88:
                bus_w(uint8_t(bus_r() | read_rom()));
                break;
            case 0x89:
                p1_ = uint8_t(p1_ | read_rom());
                port_w(1, p1_);
                break;
            case 0x8a:
                p2_ = uint8_t(p2_ | (read_rom() & p2_mask()));
                port_w(2, p2_);
                break;
            case 0x8c:
            case 0x8d:
            case 0x8e:
            case 0x8f:
                expander_operation(MCS48_EXPANDER_OP_OR, uint8_t(4 + (op & 3)));
                break;
            case 0x90:
                if (chip_ == Chip::I8042) {
                    sts_ = uint8_t((sts_ & 0x0f) | (a_ & 0xf0));
                } else {
                    extra += 1;
                    if (ext_out_) ext_out_(r(0), a_);
                }
                break;
            case 0x91:
                extra += 1;
                if (chip_ != Chip::I8042 && ext_out_) ext_out_(r(1), a_);
                break;
            case 0x92:
                cond((a_ & 0x10) != 0);
                break;
            case 0x93: {
                irq_in_progress_ = false;
                const uint8_t sp = uint8_t((psw() - 1) & 0x07);
                pc_ = ram_[8 + 2 * sp];
                pc_ = uint16_t(pc_ | (uint16_t(ram_[9 + 2 * sp]) << 8));
                set_psw(uint8_t(((pc_ >> 8) & 0xf0) | sp));
                pc_ = uint16_t(pc_ & 0xfff);
                update_regptr();
                break;
            }
            case 0x95:
                psw_ = uint8_t(psw_ ^ 0x20);
                break;
            case 0x96:
                cond(a_ != 0);
                break;
            case 0x97:
                psw_ = uint8_t(psw_ & ~0x80);
                break;
            case 0x98:
                bus_w(uint8_t(bus_r() & read_rom()));
                break;
            case 0x99:
                p1_ = uint8_t(p1_ & read_rom());
                port_w(1, p1_);
                break;
            case 0x9a:
                p2_ = uint8_t(p2_ & (read_rom() | uint8_t(~p2_mask())));
                port_w(2, p2_);
                break;
            case 0x9c:
            case 0x9d:
            case 0x9e:
            case 0x9f:
                expander_operation(MCS48_EXPANDER_OP_AND, uint8_t(4 + (op & 3)));
                break;
            case 0xa0:
            case 0xa1:
                set_ram_indir(op & 1, a_);
                break;
            case 0xa3:
                a_ = read_byte(uint16_t((pc_ & 0xf00) | a_));
                break;
            case 0xa5:
                f1_ = false;
                break;
            case 0xa7:
                psw_ = uint8_t(psw_ ^ 0x80);
                break;
            case 0xa8:
            case 0xa9:
            case 0xaa:
            case 0xab:
            case 0xac:
            case 0xad:
            case 0xae:
            case 0xaf:
                set_r(op & 7, a_);
                break;
            case 0xb0:
            case 0xb1:
                set_ram_indir(op & 1, read_rom());
                break;
            case 0xb2:
                cond((a_ & 0x20) != 0);
                break;
            case 0xb3:
                pc_ = uint16_t((pc_ & 0xf00) | read_byte(uint16_t((pc_ & 0xf00) | a_)));
                break;
            case 0xb5:
                f1_ = !f1_;
                break;
            case 0xb6:
                cond((psw_ & 0x20) != 0);
                break;
            case 0xb8:
            case 0xb9:
            case 0xba:
            case 0xbb:
            case 0xbc:
            case 0xbd:
            case 0xbe:
            case 0xbf:
                set_r(op & 7, read_rom());
                break;
            case 0xc5:
                psw_ = uint8_t(psw_ & ~0x10);
                update_regptr();
                break;
            case 0xc6:
                cond(a_ == 0);
                break;
            case 0xc7:
                a_ = uint8_t(psw() | 0x08);
                break;
            case 0xc8:
            case 0xc9:
            case 0xca:
            case 0xcb:
            case 0xcc:
            case 0xcd:
            case 0xce:
            case 0xcf:
                set_r(op & 7, uint8_t(r(op & 7) - 1));
                break;
            case 0xd0:
            case 0xd1:
                a_ = uint8_t(a_ ^ ram_indir(op & 1));
                break;
            case 0xd2:
                cond((a_ & 0x40) != 0);
                break;
            case 0xd3:
                a_ = uint8_t(a_ ^ read_rom());
                break;
            case 0xd5:
                psw_ = uint8_t(psw_ | 0x10);
                update_regptr();
                break;
            case 0xd6:
                if (chip_ == Chip::I8042) {
                    irq_polled_ = (sts_ & kStsIbf) != 0;
                    cond((sts_ & kStsIbf) == 0);
                }
                break;
            case 0xd7:
                set_psw(uint8_t(a_ & 0xf7));
                break;
            case 0xd8:
            case 0xd9:
            case 0xda:
            case 0xdb:
            case 0xdc:
            case 0xdd:
            case 0xde:
            case 0xdf:
                a_ = uint8_t(a_ ^ r(op & 7));
                break;
            case 0xe3:
                a_ = read_byte(uint16_t(0x300 | a_));
                break;
            case 0xe5:
                if (chip_ != Chip::I8042) a11_ = 0;
                break;
            case 0xe6:
                cond((psw_ & 0x80) == 0);
                break;
            case 0xe7:
                a_ = uint8_t((a_ << 1) | (a_ >> 7));
                break;
            case 0xe8:
            case 0xe9:
            case 0xea:
            case 0xeb:
            case 0xec:
            case 0xed:
            case 0xee:
            case 0xef: {
                const uint8_t value = uint8_t(r(op & 7) - 1);
                set_r(op & 7, value);
                cond(value != 0);
                break;
            }
            case 0xf0:
            case 0xf1:
                a_ = ram_indir(op & 1);
                break;
            case 0xf2:
                cond((a_ & 0x80) != 0);
                break;
            case 0xf5:
                if (chip_ != Chip::I8042) a11_ = 0x800;
                break;
            case 0xf6:
                cond((psw_ & 0x80) != 0);
                break;
            case 0xf7: {
                const uint8_t temp = a_;
                a_ = uint8_t((a_ << 1) | ((psw_ & 0x80) != 0 ? 1 : 0));
                psw_ = uint8_t((psw_ & 0x7f) | ((temp & 0x80) != 0 ? 0x80 : 0));
                break;
            }
            case 0xf8:
            case 0xf9:
            case 0xfa:
            case 0xfb:
            case 0xfc:
            case 0xfd:
            case 0xfe:
            case 0xff:
                a_ = r(op & 7);
                break;
            default:
                break;
        }

        const int spent = int(kCycles[op]) + extra;
        executed += spent;
        if (timecount_enabled_ != 0) burn_cycles(uint8_t(spent));
    }
    return executed;
}

}  // namespace dsp
