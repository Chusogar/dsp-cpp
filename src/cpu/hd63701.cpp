#include "cpu/hd63701.h"

namespace dsp {
namespace {

// Addressing mode of every opcode (direc_680x in m680x.pas).
//   0 inherent, 1 immediate byte, 2 immediate word, 3 extended address,
//   4 indexed address, 5 direct page byte, 6 indexed byte, 7 extended word,
//   8 extended byte, 9 indexed word, 10 direct page word, 11 direct page address,
//   15 invalid.
const uint8_t kMode[256] = {
    15, 0, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        // 00
    0, 0, 0, 0, 15, 15, 0, 0, 0, 0, 15, 0, 15, 15, 15, 15,    // 10
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,           // 20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,           // 30
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,      // 40
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,      // 50
    4, 1, 1, 6, 6, 15, 6, 6, 6, 6, 6, 1, 6, 6, 4, 4,          // 60
    8, 1, 1, 8, 8, 15, 8, 8, 8, 8, 8, 1, 8, 8, 3, 3,          // 70
    1, 1, 1, 2, 1, 1, 1, 15, 1, 1, 1, 1, 2, 1, 2, 15,         // 80
    5, 5, 5, 10, 5, 5, 5, 11, 5, 5, 5, 5, 10, 11, 10, 11,     // 90
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,           // a0
    8, 8, 8, 7, 8, 8, 8, 3, 8, 8, 8, 8, 7, 3, 7, 3,           // b0
    1, 1, 1, 2, 1, 1, 1, 15, 1, 1, 1, 1, 2, 15, 2, 15,        // c0
    5, 5, 5, 10, 5, 5, 5, 11, 5, 5, 5, 5, 10, 11, 10, 11,     // d0
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,           // e0
    8, 8, 8, 7, 8, 8, 8, 3, 8, 8, 8, 8, 7, 3, 7, 3,           // f0
};

// Base cycle count of every opcode (ciclos_6803 in m680x.pas).
const uint8_t kCycles6803[256] = {
    99, 2, 99, 99, 3, 3, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2,        // 00
    2, 2, 99, 99, 99, 99, 2, 2, 99, 2, 99, 2, 99, 99, 99, 99, // 10
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,           // 20
    3, 3, 4, 4, 3, 3, 3, 3, 5, 5, 3, 10, 4, 10, 9, 12,        // 30
    2, 99, 99, 2, 2, 99, 2, 2, 2, 2, 2, 99, 2, 2, 99, 2,      // 40
    2, 99, 99, 2, 2, 99, 2, 2, 2, 2, 2, 99, 2, 2, 99, 2,      // 50
    6, 99, 99, 6, 6, 99, 6, 6, 6, 6, 6, 99, 6, 6, 3, 6,       // 60
    6, 99, 99, 6, 6, 99, 6, 6, 6, 6, 6, 99, 6, 6, 3, 6,       // 70
    2, 2, 2, 4, 2, 2, 2, 99, 2, 2, 2, 2, 4, 6, 3, 99,         // 80
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4,           // 90
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,           // a0
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,           // b0
    2, 2, 2, 4, 2, 2, 2, 99, 2, 2, 2, 2, 3, 99, 3, 99,        // c0
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,           // d0
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // e0
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // f0
};

// Base cycle count of every opcode (ciclos_63701 in m680x.pas).
const uint8_t kCycles[256] = {
    99, 1, 99, 99, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,        // 00
    1, 1, 99, 99, 99, 99, 1, 1, 2, 2, 4, 1, 99, 99, 99, 99,   // 10
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,           // 20
    1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1, 10, 5, 7, 9, 12,         // 30
    1, 99, 99, 1, 1, 99, 1, 1, 1, 1, 1, 99, 1, 1, 99, 1,      // 40
    1, 99, 99, 1, 1, 99, 1, 1, 1, 1, 1, 99, 1, 1, 99, 1,      // 50
    6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,           // 60
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,           // 70
    2, 2, 2, 3, 2, 2, 2, 99, 2, 2, 2, 2, 3, 5, 3, 99,         // 80
    3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,           // 90
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // a0
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,           // b0
    2, 2, 2, 3, 2, 2, 2, 99, 2, 2, 2, 2, 3, 99, 3, 99,        // c0
    3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,           // d0
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // e0
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // f0
};

constexpr uint8_t kTrcsrTdre = 0x20;
constexpr uint8_t kTcsrEoci = 0x08;
constexpr uint8_t kTcsrOcf = 0x40;

inline uint16_t low_word(uint32_t value) { return uint16_t(value & 0xffff); }
inline uint16_t high_word(uint32_t value) { return uint16_t(value >> 16); }
inline uint32_t make_dword(uint16_t high, uint16_t low) {
    return (uint32_t(high) << 16) | low;
}

}  // namespace

HD63701::HD63701(uint32_t clock, Type type) : clock_(clock), type_(type) {
    read_ = [](uint16_t) { return uint8_t(0xff); };
    write_ = [](uint16_t, uint8_t) {};
    port_in_.fill(0xff);
    portx_in_.fill(0xff);
}

void HD63701::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void HD63701::set_port_read(int port, PortReadHandler handler) {
    port_read_[size_t(port)] = std::move(handler);
}

void HD63701::set_port_write(int port, PortWriteHandler handler) {
    port_write_[size_t(port)] = std::move(handler);
}

void HD63701::set_portx_read(int port, PortReadHandler handler) {
    portx_read_[size_t(port)] = std::move(handler);
}

void HD63701::set_portx_write(int port, PortWriteHandler handler) {
    portx_write_[size_t(port)] = std::move(handler);
}

uint8_t HD63701::read_io(uint8_t reg) {
    auto port_value = [this](int port) {
        if (port_read_[size_t(port)]) port_in_[size_t(port)] = port_read_[size_t(port)]();
        return uint8_t((port_out_[size_t(port)] & port_ddr_[size_t(port)]) |
                       (port_in_[size_t(port)] & ~port_ddr_[size_t(port)]));
    };
    auto portx_value = [this](int port) {
        if (portx_read_[size_t(port)]) portx_in_[size_t(port)] = portx_read_[size_t(port)]();
        return uint8_t((portx_out_[size_t(port)] & portx_ddr_[size_t(port)]) |
                       (portx_in_[size_t(port)] & ~portx_ddr_[size_t(port)]));
    };

    switch (reg) {
        case 0x00: return port_ddr_[0];
        case 0x01: return port_ddr_[1];
        case 0x02: return port_value(0);
        case 0x03: return port_value(1);
        case 0x04: return port_ddr_[2];
        case 0x05: return port_ddr_[3];
        case 0x06: return port_value(2);
        case 0x07: return port_value(3);
        case 0x08:
            pending_tcsr_ = 0;
            return tcsr_;
        case 0x09: return uint8_t(low_word(counter_) >> 8);
        case 0x0b:
            if ((pending_tcsr_ & kTcsrOcf) == 0) tcsr_ = uint8_t(tcsr_ & ~kTcsrOcf);
            return uint8_t(low_word(output_compare_) >> 8);
        case 0x0c:
            if ((pending_tcsr_ & kTcsrOcf) == 0) tcsr_ = uint8_t(tcsr_ & ~kTcsrOcf);
            return uint8_t(output_compare_ & 0xff);
        case 0x11:
            trcsr_read_ = true;
            return trcsr_;
        case 0x14: return ram_control_;
        case 0x15: return portx_value(0);
        case 0x17: return portx_value(1);
        default: return 0xff;
    }
}

void HD63701::write_io(uint8_t reg, uint8_t value) {
    auto update_port = [this](int port) {
        if (port_write_[size_t(port)]) {
            port_write_[size_t(port)](
                uint8_t((port_out_[size_t(port)] & port_ddr_[size_t(port)]) |
                        (port_in_[size_t(port)] & ~port_ddr_[size_t(port)])));
        }
    };
    auto update_portx = [this](int port) {
        if (portx_write_[size_t(port)]) {
            portx_write_[size_t(port)](
                uint8_t((portx_out_[size_t(port)] & portx_ddr_[size_t(port)]) |
                        (portx_in_[size_t(port)] & ~portx_ddr_[size_t(port)])));
        }
    };

    switch (reg) {
        case 0x00:
            port_ddr_[0] = value;
            update_port(0);
            break;
        case 0x01:
            port_ddr_[1] = value;
            update_port(1);
            break;
        case 0x02:
            port_out_[0] = value;
            update_port(0);
            break;
        case 0x03:
            port_out_[1] = value;
            update_port(1);
            break;
        case 0x04:
            port_ddr_[2] = value;
            update_port(2);
            break;
        case 0x05:
            port_ddr_[3] = value;
            update_port(3);
            break;
        case 0x06:
            port_out_[2] = value;
            update_port(2);
            break;
        case 0x07:
            port_out_[3] = value;
            update_port(3);
            break;
        case 0x08:
            tcsr_ = uint8_t((value & 0x1f) | (tcsr_ & 0xe0));
            pending_tcsr_ &= tcsr_;
            if (!cc.i && (tcsr_ & (kTcsrEoci | kTcsrOcf)) == (kTcsrEoci | kTcsrOcf)) {
                call_int(0xfff4);
            }
            break;
        case 0x09:
            latch09_ = value;
            counter_ = make_dword(high_word(counter_), 0xfff8);
            modified_counters();
            break;
        case 0x0a:
            counter_ = make_dword(high_word(counter_), uint16_t((latch09_ << 8) | value));
            modified_counters();
            break;
        case 0x0b:
            if (uint8_t(low_word(output_compare_) >> 8) != value) {
                output_compare_ = make_dword(
                    high_word(output_compare_),
                    uint16_t((value << 8) | (output_compare_ & 0xff)));
                modified_counters();
            }
            break;
        case 0x0c:
            if (uint8_t(output_compare_ & 0xff) != value) {
                output_compare_ = make_dword(
                    high_word(output_compare_),
                    uint16_t((low_word(output_compare_) & 0xff00) | value));
                modified_counters();
            }
            break;
        case 0x11: trcsr_ = uint8_t((trcsr_ & 0xe0) | (value & 0x1f)); break;
        case 0x13:
            if (trcsr_read_) {
                trcsr_read_ = false;
                trcsr_ = uint8_t(trcsr_ & ~kTrcsrTdre);
            }
            tdr_ = value;
            break;
        case 0x14: ram_control_ = value; break;
        case 0x15:
            portx_out_[0] = value;
            update_portx(0);
            break;
        case 0x16:
            if (portx_ddr_[1] != value) {
                portx_ddr_[1] = value;
                update_portx(1);
            }
            break;
        case 0x17:
            portx_out_[1] = value;
            update_portx(1);
            break;
        case 0x20:
            if (portx_ddr_[0] != value) {
                portx_ddr_[0] = value;
                update_portx(0);
            }
            break;
        default: break;
    }
}

uint8_t HD63701::read(uint16_t address) {
    if (type_ == Type::M6803) {
        if (address <= 0x0007) return read_io(uint8_t(address));
        if (address >= 0x0040 && address <= 0x00ff) return internal_ram_[address];
        if (address >= 0x0100) return read_(address);
        return 0xff;
    }
    if (address <= 0x001f) return read_io(uint8_t(address));
    if (address >= 0x0040 && address <= 0x01ff) return internal_ram_[address];
    if (address >= 0x0200 && address <= 0xbfff) return read_(address);
    if (address >= 0xc000) return rom_[address & 0x3fff];
    return 0xff;
}

void HD63701::write(uint16_t address, uint8_t value) {
    if (type_ == Type::M6803) {
        if (address <= 0x0007) {
            write_io(uint8_t(address), value);
            return;
        }
        if (address >= 0x0040 && address <= 0x00ff) {
            internal_ram_[address] = value;
            return;
        }
        if (address >= 0x0100 && address <= 0xefff) write_(address, value);
        return;
    }
    if (address <= 0x001f) {
        write_io(uint8_t(address), value);
        return;
    }
    if (address >= 0x0040 && address <= 0x01ff) {
        internal_ram_[address] = value;
        return;
    }
    if (address >= 0x0200 && address <= 0xbfff) write_(address, value);
    // $c000-$ffff is the internal ROM.
}

uint16_t HD63701::read_word(uint16_t address) {
    uint16_t high = read(address);
    return uint16_t((high << 8) | read(uint16_t(address + 1)));
}

void HD63701::write_word(uint16_t address, uint16_t value) {
    write(address, uint8_t(value >> 8));
    write(uint16_t(address + 1), uint8_t(value));
}

void HD63701::push(uint8_t value) {
    write(sp, value);
    sp = uint16_t(sp - 1);
}

uint8_t HD63701::pop() {
    sp = uint16_t(sp + 1);
    return read(sp);
}

void HD63701::push_word(uint16_t value) {
    push(uint8_t(value));
    push(uint8_t(value >> 8));
}

uint16_t HD63701::pop_word() {
    uint16_t high = pop();
    return uint16_t((high << 8) | pop());
}

uint8_t HD63701::get_cc() const {
    return uint8_t((cc.c ? 0x01 : 0) | (cc.v ? 0x02 : 0) | (cc.z ? 0x04 : 0) | (cc.n ? 0x08 : 0) |
                   (cc.i ? 0x10 : 0) | (cc.h ? 0x20 : 0));
}

void HD63701::set_cc(uint8_t value) {
    cc.c = (value & 0x01) != 0;
    cc.v = (value & 0x02) != 0;
    cc.z = (value & 0x04) != 0;
    cc.n = (value & 0x08) != 0;
    cc.i = (value & 0x10) != 0;
    cc.h = (value & 0x20) != 0;
}

void HD63701::reset() {
    pc_ = read_word(0xfffe);
    x = 0;
    a = 0;
    b = 0;
    sp = 0;
    cc = Flags{};
    cc.i = true;
    nmi_request_ = IrqLine::Clear;
    nmi_state_ = IrqLine::Clear;
    irq_state_ = IrqLine::Clear;
    reset_state_ = IrqLine::Clear;
    port_ddr_.fill(0);
    ram_control_ = 0x40;
    tcsr_ = kTrcsrTdre;
    counter_ = 0;
    output_compare_ = 0xffff;
    trcsr_read_ = false;
    pending_tcsr_ = 0;
    timer_next_ = output_compare_;
}

void HD63701::set_nmi(IrqLine state) {
    nmi_request_ = state;
    if (state == IrqLine::Clear) nmi_state_ = IrqLine::Clear;
}

int HD63701::call_int(uint16_t vector) {
    push_word(pc_);
    push_word(x);
    push(a);
    push(b);
    push(get_cc());
    cc.i = true;
    pc_ = read_word(vector);
    return 12;
}

void HD63701::modified_counters() {
    uint16_t high = low_word(output_compare_) >= low_word(counter_) ? high_word(counter_)
                                                                   : uint16_t(high_word(counter_) + 1);
    output_compare_ = make_dword(high, low_word(output_compare_));
    timer_next_ = output_compare_;
}

void HD63701::check_timer_event() {
    if (counter_ >= output_compare_) {
        output_compare_ = make_dword(uint16_t(high_word(output_compare_) + 1),
                                     low_word(output_compare_));
        pending_tcsr_ |= kTcsrOcf;
        tcsr_ |= kTcsrOcf;
        if (!cc.i && (tcsr_ & kTcsrEoci) != 0) call_int(0xfff4);
    }
    timer_next_ = output_compare_;
}

uint8_t HD63701::op_neg(uint8_t value) {
    uint16_t result = uint16_t(-int16_t(value));
    cc.z = (result & 0xff) == 0;
    cc.n = (result & 0x80) != 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((0 ^ value ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t HD63701::op_com(uint8_t value) {
    uint8_t result = uint8_t(~value);
    cc.v = false;
    cc.c = true;
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    return result;
}

uint8_t HD63701::op_lsr(uint8_t value) {
    uint8_t result = uint8_t(value >> 1);
    cc.n = false;
    cc.c = (value & 1) != 0;
    cc.z = result == 0;
    cc.v = cc.n != cc.c;
    return result;
}

uint8_t HD63701::op_asl(uint8_t value) {
    uint16_t result = uint16_t(value << 1);
    cc.z = (result & 0xff) == 0;
    cc.c = (result & 0x100) != 0;
    cc.n = (result & 0x80) != 0;
    cc.v = ((result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t HD63701::op_ror(uint8_t value) {
    uint8_t result = uint8_t((value >> 1) | (cc.c ? 0x80 : 0));
    cc.c = (value & 0x01) != 0;
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    cc.v = cc.n != cc.c;
    return result;
}

uint8_t HD63701::op_rol(uint8_t value) {
    uint16_t result = uint16_t((value << 1) | (cc.c ? 1 : 0));
    cc.z = (result & 0xff) == 0;
    cc.c = (result & 0x100) != 0;
    cc.n = (result & 0x80) != 0;
    cc.v = ((result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t HD63701::op_asr(uint8_t value) {
    cc.c = (value & 0x01) != 0;
    uint8_t result = uint8_t(value >> 1);
    result = uint8_t(result | ((result & 0x40) << 1));
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    cc.v = cc.n != cc.c;
    return result;
}

uint8_t HD63701::op_dec(uint8_t value) {
    uint8_t result = uint8_t(value - 1);
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    cc.v = result == 0x7f;
    return result;
}

uint8_t HD63701::op_inc(uint8_t value) {
    uint8_t result = uint8_t(value + 1);
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    cc.v = result == 0x80;
    return result;
}

void HD63701::op_tst(uint8_t value) {
    cc.v = false;
    cc.c = false;
    cc.z = value == 0;
    cc.n = (value & 0x80) != 0;
}

uint8_t HD63701::op_sub(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left - right);
    cc.z = (result & 0xff) == 0;
    cc.n = (result & 0x80) != 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t HD63701::op_sbc(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left - right - (cc.c ? 1 : 0));
    cc.z = (result & 0xff) == 0;
    cc.n = (result & 0x80) != 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t HD63701::op_and(uint8_t left, uint8_t right) {
    uint8_t result = uint8_t(left & right);
    cc.v = false;
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    return result;
}

uint8_t HD63701::op_eor(uint8_t left, uint8_t right) {
    uint8_t result = uint8_t(left ^ right);
    cc.v = false;
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    return result;
}

uint8_t HD63701::op_adc(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left + right + (cc.c ? 1 : 0));
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    cc.h = ((left ^ right ^ result) & 0x10) != 0;
    return uint8_t(result);
}

uint8_t HD63701::op_or(uint8_t left, uint8_t right) {
    uint8_t result = uint8_t(left | right);
    cc.v = false;
    cc.z = result == 0;
    cc.n = (result & 0x80) != 0;
    return result;
}

uint8_t HD63701::op_add(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left + right);
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    cc.h = ((left ^ right ^ result) & 0x10) != 0;
    return uint8_t(result);
}

int HD63701::run(int cycles) {
    int executed = 0;
    while (executed < cycles) {
        if (halt_state_ != IrqLine::Clear) return cycles;
        if (reset_state_ != IrqLine::Clear) {
            IrqLine requested = reset_state_;
            reset();
            if (requested == IrqLine::Assert) {
                reset_state_ = IrqLine::Assert;
                return cycles;
            }
        }

        extra_cycles_ = 0;
        if (nmi_request_ != IrqLine::Clear) {
            if (nmi_state_ == IrqLine::Clear) extra_cycles_ = call_int(0xfffc);
            if (nmi_request_ == IrqLine::Pulse) nmi_request_ = IrqLine::Clear;
            if (nmi_request_ == IrqLine::Assert) nmi_state_ = IrqLine::Assert;
        } else if (irq_state_ != IrqLine::Clear && !cc.i) {
            irq_state_ = IrqLine::Clear;
            extra_cycles_ = call_int(0xfff8);
        } else if (!cc.i && (tcsr_ & (kTcsrEoci | kTcsrOcf)) == (kTcsrEoci | kTcsrOcf)) {
            call_int(0xfff4);
        }

        // CLEANUP_COUNTERS(): keep the counter inside its low word.
        output_compare_ = make_dword(uint16_t(high_word(output_compare_) - high_word(counter_)),
                                     low_word(output_compare_));
        counter_ = low_word(counter_);
        timer_next_ = output_compare_;

        uint8_t opcode = read(pc_++);
        switch (kMode[opcode]) {
            case 0: break;  // inherent
            case 1: operand_ = read(pc_++); break;
            case 2:
                operand_word_ = read_word(pc_);
                pc_ = uint16_t(pc_ + 2);
                break;
            case 3:
                address_ = read_word(pc_);
                pc_ = uint16_t(pc_ + 2);
                break;
            case 4: address_ = uint16_t(x + read(pc_++)); break;
            case 5:
                address_ = read(pc_++);
                operand_ = read(address_);
                break;
            case 6:
                address_ = uint16_t(x + read(pc_++));
                operand_ = read(address_);
                break;
            case 7:
                address_ = read_word(pc_);
                pc_ = uint16_t(pc_ + 2);
                operand_word_ = read_word(address_);
                break;
            case 8:
                address_ = read_word(pc_);
                pc_ = uint16_t(pc_ + 2);
                operand_ = read(address_);
                break;
            case 9:
                address_ = uint16_t(x + read(pc_++));
                operand_word_ = read_word(address_);
                break;
            case 10:
                address_ = read(pc_++);
                operand_word_ = read_word(address_);
                break;
            case 11: address_ = read(pc_++); break;
            default: break;  // invalid opcode
        }

        auto branch = [this](bool condition) {
            if (condition) pc_ = uint16_t(pc_ + int8_t(operand_));
        };
        // AIM/OIM/TIM read a second operand byte holding the address.
        auto immediate_address = [this](bool indexed) {
            uint16_t target = read(pc_++);
            return indexed ? uint16_t(x + target) : target;
        };

        switch (opcode) {
            case 0x01: break;  // nop
            case 0x04: {       // lsrd
                cc.n = false;
                uint16_t value = d();
                cc.c = (value & 1) != 0;
                value = uint16_t(value >> 1);
                cc.z = value == 0;
                cc.v = cc.n != cc.c;
                set_d(value);
                break;
            }
            case 0x05: {  // asld
                uint32_t value = uint32_t(d()) << 1;
                cc.n = (value & 0x8000) != 0;
                cc.z = (value & 0xffff) == 0;
                cc.c = (value & 0x10000) != 0;
                cc.v = ((value ^ (value >> 1)) & 0x8000) != 0;
                set_d(uint16_t(value));
                break;
            }
            case 0x06: set_cc(a); break;  // tap
            case 0x07: a = get_cc(); break;  // tpa
            case 0x08:                       // inx
                x = uint16_t(x + 1);
                cc.z = x == 0;
                break;
            case 0x09:  // dex
                x = uint16_t(x - 1);
                cc.z = x == 0;
                break;
            case 0x0a: cc.v = false; break;  // clv
            case 0x0b: cc.v = true; break;   // sev
            case 0x0c: cc.c = false; break;  // clc
            case 0x0d: cc.c = true; break;   // sec
            case 0x0e: cc.i = false; break;  // cli
            case 0x0f: cc.i = true; break;   // sei
            case 0x10: a = op_sub(a, b); break;  // sba
            case 0x11: op_sub(a, b); break;      // cba
            case 0x12:
            case 0x13: x = uint16_t(x + read(uint16_t(sp + 1))); break;  // undocumented asx1/asx2
            case 0x16:                                                  // tab
                b = a;
                cc.v = false;
                cc.z = b == 0;
                cc.n = (b & 0x80) != 0;
                break;
            case 0x17:  // tba
                a = b;
                cc.v = false;
                cc.z = a == 0;
                cc.n = (a & 0x80) != 0;
                break;
            case 0x18: {  // xgdx
                uint16_t value = x;
                x = d();
                set_d(value);
                break;
            }
            case 0x19: {  // daa
                uint8_t msn = uint8_t(a & 0xf0);
                uint8_t lsn = uint8_t(a & 0x0f);
                uint16_t correction = 0;
                if (lsn > 0x09 || cc.h) correction |= 0x06;
                if (msn > 0x80 && lsn > 0x09) correction |= 0x60;
                if (msn > 0x90 || cc.c) correction |= 0x60;
                uint16_t result = uint16_t(correction + a);
                cc.z = (result & 0xff) == 0;
                cc.n = (result & 0x80) != 0;
                cc.c = cc.c || (result & 0x100) != 0;
                cc.v = false;
                a = uint8_t(result);
                break;
            }
            case 0x1b: a = op_add(a, b); break;  // aba
            case 0x20: branch(true); break;
            case 0x22: branch(!(cc.c || cc.z)); break;  // bhi
            case 0x23: branch(cc.c || cc.z); break;     // bls
            case 0x24: branch(!cc.c); break;            // bcc
            case 0x25: branch(cc.c); break;             // bcs
            case 0x26: branch(!cc.z); break;            // bne
            case 0x27: branch(cc.z); break;             // beq
            case 0x28: branch(!cc.v); break;            // bvc
            case 0x29: branch(cc.v); break;             // bvs
            case 0x2a: branch(!cc.n); break;            // bpl
            case 0x2b: branch(cc.n); break;             // bmi
            case 0x2c: branch(cc.n == cc.v); break;     // bge
            case 0x2d: branch(cc.n != cc.v); break;     // blt
            case 0x2e: branch(cc.n == cc.v && !cc.z); break;    // bgt
            case 0x2f: branch(cc.n != cc.v || cc.z); break;     // ble
            case 0x30:  // tsx
                x = uint16_t(sp + 1);
                break;
            case 0x31: sp = uint16_t(sp + 1); break;  // ins
            case 0x32: a = pop(); break;              // pula
            case 0x33: b = pop(); break;              // pulb
            case 0x34: sp = uint16_t(sp - 1); break;  // des
            case 0x35: sp = uint16_t(x - 1); break;   // txs
            case 0x36: push(a); break;                // psha
            case 0x37: push(b); break;                // pshb
            case 0x38: x = pop_word(); break;         // pulx
            case 0x39: pc_ = pop_word(); break;       // rts
            case 0x3a: x = uint16_t(x + b); break;    // abx
            case 0x3b:                                // rti
                set_cc(pop());
                b = pop();
                a = pop();
                x = pop_word();
                pc_ = pop_word();
                break;
            case 0x3c: push_word(x); break;  // pshx
            case 0x3d:                       // mul
                set_d(uint16_t(a * b));
                cc.c = (d() & 0x80) != 0;
                break;
            case 0x3e: break;                 // wai, unused by this board
            case 0x3f: call_int(0xfffa); break;  // swi
            case 0x40: a = op_neg(a); break;
            case 0x43: a = op_com(a); break;
            case 0x44: a = op_lsr(a); break;
            case 0x46: a = op_ror(a); break;
            case 0x47: a = op_asr(a); break;
            case 0x48: a = op_asl(a); break;
            case 0x49: a = op_rol(a); break;
            case 0x4a: a = op_dec(a); break;
            case 0x4c: a = op_inc(a); break;
            case 0x4d: op_tst(a); break;
            case 0x4f:  // clra
                a = 0;
                cc.z = true;
                cc.n = false;
                cc.v = false;
                cc.c = false;
                break;
            case 0x50: b = op_neg(b); break;
            case 0x53: b = op_com(b); break;
            case 0x54: b = op_lsr(b); break;
            case 0x56: b = op_ror(b); break;
            case 0x57: b = op_asr(b); break;
            case 0x58: b = op_asl(b); break;
            case 0x59: b = op_rol(b); break;
            case 0x5a: b = op_dec(b); break;
            case 0x5c: b = op_inc(b); break;
            case 0x5d: op_tst(b); break;
            case 0x5f:  // clrb
                b = 0;
                cc.z = true;
                cc.n = false;
                cc.v = false;
                cc.c = false;
                break;
            case 0x60: write(address_, op_neg(read(address_))); break;
            case 0x70: write(address_, op_neg(operand_)); break;
            case 0x61:
            case 0x71: {  // aim
                uint16_t target = immediate_address(opcode == 0x61);
                uint8_t result = uint8_t(read(target) & operand_);
                cc.v = false;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                write(target, result);
                break;
            }
            case 0x62:
            case 0x72: {  // oim
                uint16_t target = immediate_address(opcode == 0x62);
                uint8_t result = uint8_t(read(target) | operand_);
                cc.v = false;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                write(target, result);
                break;
            }
            case 0x6b:
            case 0x7b: {  // tim
                uint16_t target = immediate_address(opcode == 0x6b);
                uint8_t result = uint8_t(read(target) & operand_);
                cc.v = false;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                break;
            }
            case 0x63:
            case 0x73: write(address_, op_com(operand_)); break;
            case 0x64:
            case 0x74: write(address_, op_lsr(operand_)); break;
            case 0x66:
            case 0x76: write(address_, op_ror(operand_)); break;
            case 0x67:
            case 0x77: write(address_, op_asr(operand_)); break;
            case 0x68:
            case 0x78: write(address_, op_asl(operand_)); break;
            case 0x69:
            case 0x79: write(address_, op_rol(operand_)); break;
            case 0x6a:
            case 0x7a: write(address_, op_dec(operand_)); break;
            case 0x6c:
            case 0x7c: write(address_, op_inc(operand_)); break;
            case 0x6d:
            case 0x7d: op_tst(operand_); break;
            case 0x6e:
            case 0x7e: pc_ = address_; break;  // jmp
            case 0x6f:
            case 0x7f:  // clr
                write(address_, 0);
                cc.z = true;
                cc.n = false;
                cc.v = false;
                cc.c = false;
                break;
            case 0x80:
            case 0x90:
            case 0xa0:
            case 0xb0: a = op_sub(a, operand_); break;  // suba
            case 0x81:
            case 0x91:
            case 0xa1:
            case 0xb1: op_sub(a, operand_); break;  // cmpa
            case 0x82:
            case 0x92:
            case 0xa2:
            case 0xb2: a = op_sbc(a, operand_); break;  // sbca
            case 0x83:
            case 0x93:
            case 0xa3:
            case 0xb3: {  // subd
                uint32_t result = uint32_t(d()) - operand_word_;
                cc.z = (result & 0xffff) == 0;
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.v = ((d() ^ operand_word_ ^ result ^ (result >> 1)) & 0x8000) != 0;
                set_d(uint16_t(result));
                break;
            }
            case 0x84:
            case 0x94:
            case 0xa4:
            case 0xb4: a = op_and(a, operand_); break;  // anda
            case 0x85:
            case 0x95:
            case 0xa5:
            case 0xb5: op_and(a, operand_); break;  // bita
            case 0x86:
            case 0x96:
            case 0xa6:
            case 0xb6:  // lda
                a = operand_;
                cc.v = false;
                cc.z = a == 0;
                cc.n = (a & 0x80) != 0;
                break;
            case 0x97:
            case 0xa7:
            case 0xb7:  // sta
                cc.v = false;
                cc.z = a == 0;
                cc.n = (a & 0x80) != 0;
                write(address_, a);
                break;
            case 0x88:
            case 0x98:
            case 0xa8:
            case 0xb8: a = op_eor(a, operand_); break;  // eora
            case 0x89:
            case 0x99:
            case 0xa9:
            case 0xb9: a = op_adc(a, operand_); break;  // adca
            case 0x8a:
            case 0x9a:
            case 0xaa:
            case 0xba: a = op_or(a, operand_); break;  // ora
            case 0x8b:
            case 0x9b:
            case 0xab:
            case 0xbb: a = op_add(a, operand_); break;  // adda
            case 0x8c:
            case 0x9c:
            case 0xac:
            case 0xbc: {  // cpx
                uint32_t result = uint32_t(x) - operand_word_;
                cc.z = (result & 0xffff) == 0;
                cc.n = (result & 0x8000) != 0;
                cc.v = ((x ^ operand_word_ ^ result ^ (result >> 1)) & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                break;
            }
            case 0x8d:  // bsr
                push_word(pc_);
                pc_ = uint16_t(pc_ + int8_t(operand_));
                break;
            case 0x9d:
            case 0xad:
            case 0xbd:  // jsr
                push_word(pc_);
                pc_ = address_;
                break;
            case 0x8e:
            case 0x9e:
            case 0xae:
            case 0xbe:  // lds
                sp = operand_word_;
                cc.v = false;
                cc.z = sp == 0;
                cc.n = (sp & 0x8000) != 0;
                break;
            case 0x9f:
            case 0xaf:
            case 0xbf:  // sts
                cc.v = false;
                cc.z = sp == 0;
                cc.n = (sp & 0x8000) != 0;
                write_word(address_, sp);
                break;
            case 0xc0:
            case 0xd0:
            case 0xe0:
            case 0xf0: b = op_sub(b, operand_); break;  // subb
            case 0xc1:
            case 0xd1:
            case 0xe1:
            case 0xf1: op_sub(b, operand_); break;  // cmpb
            case 0xc2:
            case 0xd2:
            case 0xe2:
            case 0xf2: b = op_sbc(b, operand_); break;  // sbcb
            case 0xc3:
            case 0xd3:
            case 0xe3:
            case 0xf3: {  // addd
                uint32_t result = uint32_t(d()) + operand_word_;
                cc.z = (result & 0xffff) == 0;
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.v = ((d() ^ operand_word_ ^ result ^ (result >> 1)) & 0x8000) != 0;
                set_d(uint16_t(result));
                break;
            }
            case 0xc4:
            case 0xd4:
            case 0xe4:
            case 0xf4: b = op_and(b, operand_); break;  // andb
            case 0xc5:
            case 0xd5:
            case 0xe5:
            case 0xf5: op_and(b, operand_); break;  // bitb
            case 0xc6:
            case 0xd6:
            case 0xe6:
            case 0xf6:  // ldb
                b = operand_;
                cc.v = false;
                cc.z = b == 0;
                cc.n = (b & 0x80) != 0;
                break;
            case 0xd7:
            case 0xe7:
            case 0xf7:  // stb
                cc.v = false;
                cc.z = b == 0;
                cc.n = (b & 0x80) != 0;
                write(address_, b);
                break;
            case 0xc8:
            case 0xd8:
            case 0xe8:
            case 0xf8: b = op_eor(b, operand_); break;  // eorb
            case 0xc9:
            case 0xd9:
            case 0xe9:
            case 0xf9: b = op_adc(b, operand_); break;  // adcb
            case 0xca:
            case 0xda:
            case 0xea:
            case 0xfa: b = op_or(b, operand_); break;  // orb
            case 0xcb:
            case 0xdb:
            case 0xeb:
            case 0xfb: b = op_add(b, operand_); break;  // addb
            case 0xcc:
            case 0xdc:
            case 0xec:
            case 0xfc:  // ldd
                set_d(operand_word_);
                cc.v = false;
                cc.z = operand_word_ == 0;
                cc.n = (operand_word_ & 0x8000) != 0;
                break;
            case 0xdd:
            case 0xed:
            case 0xfd:  // std
                cc.v = false;
                cc.z = d() == 0;
                cc.n = (d() & 0x8000) != 0;
                write_word(address_, d());
                break;
            case 0xce:
            case 0xde:
            case 0xee:
            case 0xfe:  // ldx
                x = operand_word_;
                cc.v = false;
                cc.z = x == 0;
                cc.n = (x & 0x8000) != 0;
                break;
            case 0xdf:
            case 0xef:
            case 0xff:  // stx
                cc.v = false;
                cc.z = x == 0;
                cc.n = (x & 0x8000) != 0;
                write_word(address_, x);
                break;
            default: break;
        }

        const uint8_t* cycles = (type_ == Type::M6803) ? kCycles6803 : kCycles;
        int elapsed = cycles[opcode] + extra_cycles_;
        if (elapsed >= 99) elapsed = 1;  // invalid opcode
        executed += elapsed;
        if (type_ != Type::M6803) {
            counter_ += uint32_t(elapsed);
            if (counter_ >= timer_next_) check_timer_event();
        }
        if (cycle_handler_) cycle_handler_(elapsed);
    }
    return executed;
}

}  // namespace dsp
