#include "cpu/m6809.h"

namespace dsp {
namespace {

// Base cycle count of every opcode (estados_t in m6809.pas).
const uint8_t kCycles[256] = {
    6, 6, 6, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 3, 3, 6,  // 00 direct
    0, 0, 2, 4, 0, 0, 4, 9, 0, 2, 3, 0, 3, 2, 8, 6,  // 10
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 20 branch
    2, 2, 2, 2, 5, 4, 5, 4, 0, 4, 3, 4, 16, 11, 0, 0,  // 30
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 1, 0, 1, 2, 0, 1,  // 40 register A
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 1, 0, 1, 2, 0, 1,  // 50 register B
    4, 0, 0, 4, 4, 0, 4, 4, 4, 4, 4, 0, 4, 4, 1, 4,  // 60 indexed
    7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 4, 7,  // 70 extended
    2, 2, 2, 5, 2, 2, 2, 0, 2, 2, 2, 2, 5, 7, 4, 0,  // 80 immediate
    4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 4, 7, 7, 6, 6,  // 90 direct
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 5, 3, 3,  // a0 indexed
    5, 5, 5, 8, 5, 5, 5, 5, 5, 5, 5, 5, 8, 8, 7, 7,  // b0 extended
    2, 2, 2, 5, 2, 2, 2, 0, 2, 2, 2, 2, 4, 0, 4, 0,  // c0 immediate
    4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6,  // d0 direct
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,  // e0 indexed
    5, 5, 5, 8, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 7,  // f0 extended
};

// Addressing mode of every opcode (paginacion in m6809.pas).
//   0 implied, 1 direct page address, 2 immediate byte, 3 extended address,
//   4 indexed address, 5 direct page byte, 6 indexed byte, 7 extended byte,
//   8 direct page word, 9 indexed word, 10 extended word.
const uint8_t kMode[256] = {
    1, 1, 1, 1, 1, 15, 1, 1, 1, 1, 1, 15, 1, 1, 1, 1,          // 00
    0, 0, 0, 0, 15, 15, 3, 3, 15, 0, 2, 15, 2, 0, 2, 2,         // 10
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,             // 20
    4, 4, 4, 4, 2, 2, 2, 2, 15, 0, 0, 0, 2, 0, 15, 15,          // 30
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,        // 40
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,        // 50
    4, 15, 15, 4, 4, 15, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4,         // 60
    3, 15, 15, 3, 3, 15, 3, 3, 3, 3, 3, 15, 3, 3, 3, 3,         // 70
    2, 2, 2, 3, 2, 2, 2, 15, 2, 2, 2, 2, 3, 2, 3, 15,           // 80
    5, 5, 5, 8, 5, 5, 5, 1, 5, 5, 5, 5, 8, 1, 8, 1,             // 90
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,             // a0
    7, 7, 7, 10, 7, 7, 7, 3, 7, 7, 7, 7, 10, 3, 10, 3,          // b0
    2, 2, 2, 3, 2, 2, 2, 15, 2, 2, 2, 2, 3, 15, 3, 15,          // c0
    5, 5, 5, 8, 5, 5, 5, 1, 5, 5, 5, 5, 8, 1, 8, 1,             // d0
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,             // e0
    7, 7, 7, 10, 7, 7, 7, 3, 7, 7, 7, 7, 10, 3, 10, 3,          // f0
};

// Extra cycles and addressing mode of the $10 / $11 prefixed opcodes.
const uint8_t kCycles1X[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10
    0, 0, 5, 5, 5, 5, 5, 5, 0, 0, 5, 5, 5, 5, 5, 5,  // 20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 30
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 50
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 70
    0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 4, 0,  // 80
    0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 6, 6,  // 90
    0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 4, 4,  // a0
    0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 7, 7,  // b0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0,  // c0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6,  // d0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4,  // e0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7,  // f0
};

const uint8_t kMode1X[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10
    0, 0, 3, 3, 3, 3, 3, 3, 0, 0, 3, 3, 3, 3, 3, 3,  // 20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 30
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 50
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 70
    0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0,  // 80
    0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 5, 1,  // 90
    0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 6, 4,  // a0
    0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 7, 3,  // b0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,  // c0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 1,  // d0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 4,  // e0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 3,  // f0
};

}  // namespace

M6809::M6809(uint32_t clock) : clock_(clock) {
    read_ = [](uint16_t) { return uint8_t(0xff); };
    write_ = [](uint16_t, uint8_t) {};
}

void M6809::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

uint16_t M6809::read_word(uint16_t address) {
    uint16_t high = read(address);
    return uint16_t((high << 8) | read(uint16_t(address + 1)));
}

void M6809::write_word(uint16_t address, uint16_t value) {
    write(address, uint8_t(value >> 8));
    write(uint16_t(address + 1), uint8_t(value));
}

uint8_t M6809::fetch() { return read(pc_++); }

uint8_t M6809::fetch_opcode() {
    uint8_t value = opcode_read_ ? opcode_read_(pc_) : read(pc_);
    pc_ = uint16_t(pc_ + 1);
    return value;
}

uint16_t M6809::fetch_word() {
    uint16_t value = read_word(pc_);
    pc_ = uint16_t(pc_ + 2);
    return value;
}

void M6809::push_s(uint8_t value) { write(--s, value); }
uint8_t M6809::pop_s() { return read(s++); }

void M6809::push_sw(uint16_t value) {
    s = uint16_t(s - 2);
    write(uint16_t(s + 1), uint8_t(value));
    write(s, uint8_t(value >> 8));
}

uint16_t M6809::pop_sw() {
    uint16_t value = read_word(s);
    s = uint16_t(s + 2);
    return value;
}

void M6809::push_u(uint8_t value) { write(--u, value); }
uint8_t M6809::pop_u() { return read(u++); }

void M6809::push_uw(uint16_t value) {
    u = uint16_t(u - 2);
    write(uint16_t(u + 1), uint8_t(value));
    write(u, uint8_t(value >> 8));
}

uint16_t M6809::pop_uw() {
    uint16_t value = read_word(u);
    u = uint16_t(u + 2);
    return value;
}

uint8_t M6809::get_cc() const {
    return uint8_t((cc.e ? 0x80 : 0) | (cc.f ? 0x40 : 0) | (cc.h ? 0x20 : 0) | (cc.i ? 0x10 : 0) |
                   (cc.n ? 0x08 : 0) | (cc.z ? 0x04 : 0) | (cc.v ? 0x02 : 0) | (cc.c ? 0x01 : 0));
}

void M6809::set_cc(uint8_t value) {
    cc.e = (value & 0x80) != 0;
    cc.f = (value & 0x40) != 0;
    cc.h = (value & 0x20) != 0;
    cc.i = (value & 0x10) != 0;
    cc.n = (value & 0x08) != 0;
    cc.z = (value & 0x04) != 0;
    cc.v = (value & 0x02) != 0;
    cc.c = (value & 0x01) != 0;
}

void M6809::reset() {
    pc_ = read_word(0xfffe);
    dp = 0;
    set_cc(0x50);
    nmi_request_ = IrqLine::Clear;
    nmi_state_ = IrqLine::Clear;
    irq_state_ = IrqLine::Clear;
    firq_state_ = IrqLine::Clear;
    cwai_ = false;
    stack_init_ = false;
}

void M6809::set_nmi(IrqLine state) {
    nmi_request_ = state;
    if (state == IrqLine::Clear) nmi_state_ = IrqLine::Clear;
}

int M6809::call_nmi() {
    if (nmi_state_ != IrqLine::Clear) return 0;
    if (!stack_init_) return 0;
    int cycles = 0;
    if (cwai_) {
        cwai_ = false;
        cycles = 6;
    } else {
        push_sw(pc_);
        push_sw(u);
        push_sw(y);
        push_sw(x);
        push_s(dp);
        push_s(b);
        push_s(a);
        cc.e = true;
        push_s(get_cc());
        cycles = 19;
    }
    cc.i = true;
    cc.f = true;
    pc_ = read_word(0xfffc);
    if (nmi_request_ == IrqLine::Pulse) nmi_request_ = IrqLine::Clear;
    if (nmi_request_ == IrqLine::Assert) nmi_state_ = IrqLine::Assert;
    return cycles;
}

int M6809::call_irq() {
    int cycles = 0;
    if (cwai_) {
        cwai_ = false;
        cycles = 6;
    } else {
        push_sw(pc_);
        push_sw(u);
        push_sw(y);
        push_sw(x);
        push_s(dp);
        push_s(b);
        push_s(a);
        cc.e = true;
        push_s(get_cc());
        cycles = 19;
    }
    pc_ = read_word(0xfff8);
    cc.i = true;
    if (irq_state_ == IrqLine::Hold) irq_state_ = IrqLine::Clear;
    return cycles;
}

int M6809::call_firq() {
    int cycles = 0;
    if (cwai_) {
        cwai_ = false;
        cycles = 6;
    } else {
        cc.e = false;
        push_sw(pc_);
        push_s(get_cc());
        cycles = 10;
    }
    cc.f = true;
    cc.i = true;
    pc_ = read_word(0xfff6);
    if (firq_state_ == IrqLine::Hold) firq_state_ = IrqLine::Clear;
    return cycles;
}

uint16_t* M6809::index_register(uint8_t postbyte) {
    switch (postbyte & 0x60) {
        case 0x00: return &x;
        case 0x20: return &y;
        case 0x40: return &u;
        default: return &s;
    }
}

uint16_t M6809::get_indexed() {
    uint8_t postbyte = fetch();
    uint16_t* reg = index_register(postbyte);
    uint16_t address = 0;

    if ((postbyte & 0x80) != 0) {
        switch (postbyte & 0x0f) {
            case 0x0:  // ,R+
                address = *reg;
                *reg = uint16_t(*reg + 1);
                extra_cycles_ += 4;
                break;
            case 0x1:  // ,R++
                address = *reg;
                *reg = uint16_t(*reg + 2);
                extra_cycles_ += 5;
                break;
            case 0x2:  // ,-R
                *reg = uint16_t(*reg - 1);
                address = *reg;
                extra_cycles_ += 4;
                break;
            case 0x3:  // ,--R
                *reg = uint16_t(*reg - 2);
                address = *reg;
                extra_cycles_ += 5;
                break;
            case 0x4:  // ,R
                address = *reg;
                extra_cycles_ += 2;
                break;
            case 0x5:  // B,R
                address = uint16_t(*reg + int8_t(b));
                extra_cycles_ += 3;
                break;
            case 0x6:  // A,R
                address = uint16_t(*reg + int8_t(a));
                extra_cycles_ += 3;
                break;
            case 0x8:  // n8,R
                address = uint16_t(*reg + int8_t(fetch()));
                extra_cycles_ += 3;
                break;
            case 0x9:  // n16,R
                address = uint16_t(*reg + int16_t(fetch_word()));
                extra_cycles_ += 6;
                break;
            case 0xb:  // D,R
                address = uint16_t(*reg + int16_t(d()));
                extra_cycles_ += 6;
                break;
            case 0xc: {  // n8,PCR
                int8_t offset = int8_t(fetch());
                address = uint16_t(pc_ + offset);
                extra_cycles_ += 2;
                break;
            }
            case 0xd: {  // n16,PCR
                uint16_t offset = fetch_word();
                address = uint16_t(pc_ + offset);
                extra_cycles_ += 7;
                break;
            }
            case 0xf:  // [n16]
                address = fetch_word();
                extra_cycles_ += 4;
                break;
            default:  // undefined on the 6809
                address = *reg;
                extra_cycles_ += 2;
                break;
        }
        if ((postbyte & 0x10) != 0) {  // indirect
            address = read_word(address);
            extra_cycles_ += 2;
        }
    } else {  // 5 bit offset
        uint8_t offset = uint8_t(postbyte & 0x0f);
        address = ((postbyte & 0x10) == 0) ? uint16_t(*reg + offset)
                                           : uint16_t(*reg - (16 - offset));
        extra_cycles_ += 3;
    }
    return address;
}

uint16_t M6809::transfer_source(uint8_t code) const {
    switch (code) {
        case 0x0: return d();
        case 0x1: return x;
        case 0x2: return y;
        case 0x3: return u;
        case 0x4: return s;
        case 0x5: return pc_;
        case 0x8: return a;
        case 0x9: return b;
        case 0xa: return get_cc();
        case 0xb: return dp;
        default: return 0xffff;
    }
}

void M6809::transfer_target(uint8_t code, uint16_t value) {
    switch (code) {
        case 0x0: set_d(value); break;
        case 0x1: x = value; break;
        case 0x2: y = value; break;
        case 0x3: u = value; break;
        case 0x4: s = value; break;
        case 0x5: pc_ = value; break;
        case 0x8: a = uint8_t(value); break;
        case 0x9: b = uint8_t(value); break;
        case 0xa: set_cc(uint8_t(value)); break;
        case 0xb: dp = uint8_t(value); break;
        default: break;
    }
}

void M6809::tfr(uint8_t value) {
    transfer_target(uint8_t(value & 0x0f), transfer_source(uint8_t(value >> 4)));
}

void M6809::exg(uint8_t value) {
    uint16_t source = transfer_source(uint8_t(value >> 4));
    uint16_t target = transfer_source(uint8_t(value & 0x0f));
    transfer_target(uint8_t(value >> 4), target);
    transfer_target(uint8_t(value & 0x0f), source);
}

uint8_t M6809::op_neg(uint8_t value) {
    uint16_t result = uint16_t(0u - value);
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((0 ^ value ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t M6809::op_com(uint8_t value) {
    uint8_t result = uint8_t(~value);
    cc.v = false;
    cc.c = true;
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    return result;
}

uint8_t M6809::op_lsr(uint8_t value) {
    uint8_t result = uint8_t(value >> 1);
    cc.z = result == 0;
    cc.n = false;
    cc.c = (value & 1) != 0;
    return result;
}

uint8_t M6809::op_ror(uint8_t value) {
    uint8_t result = uint8_t((value >> 1) | (cc.c ? 0x80 : 0));
    cc.c = (value & 1) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    return result;
}

uint8_t M6809::op_asr(uint8_t value) {
    uint8_t result = uint8_t((value & 0x80) | (value >> 1));
    cc.c = (value & 1) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    return result;
}

uint8_t M6809::op_asl(uint8_t value) {
    uint16_t result = uint16_t(value << 1);
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.c = (result & 0x100) != 0;
    cc.v = ((value ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t M6809::op_rol(uint8_t value) {
    uint16_t result = uint16_t((value << 1) | (cc.c ? 1 : 0));
    cc.c = (value & 0x80) != 0;
    cc.v = ((value ^ result ^ (result >> 1)) & 0x80) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    return uint8_t(result);
}

uint8_t M6809::op_dec(uint8_t value) {
    uint16_t result = uint16_t(value - 1);
    cc.z = (result & 0xff) == 0;
    cc.n = (result & 0x80) != 0;
    cc.v = ((value ^ 1 ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t M6809::op_inc(uint8_t value) {
    uint16_t result = uint16_t(value + 1);
    cc.z = (result & 0xff) == 0;
    cc.n = (result & 0x80) != 0;
    cc.v = ((value ^ 1 ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

void M6809::op_tst(uint8_t value) {
    cc.v = ((value >> 1) & 0x80) != 0;
    cc.n = (value & 0x80) != 0;
    cc.z = value == 0;
}

uint8_t M6809::op_sub8(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left - right);
    cc.c = (result & 0x100) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint16_t M6809::op_sub16(uint16_t left, uint16_t right) {
    uint32_t result = uint32_t(left) - uint32_t(right);
    cc.c = (result & 0x10000) != 0;
    cc.n = (result & 0x8000) != 0;
    cc.z = (result & 0xffff) == 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x8000) != 0;
    return uint16_t(result);
}

uint8_t M6809::op_sbc(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left - right - (cc.c ? 1 : 0));
    cc.c = (result & 0x100) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t M6809::op_and(uint8_t left, uint8_t right) {
    uint8_t result = uint8_t(left & right);
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    cc.v = false;
    return result;
}

uint8_t M6809::op_eor(uint8_t left, uint8_t right) {
    uint8_t result = uint8_t(left ^ right);
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    cc.v = false;
    return result;
}

uint8_t M6809::op_adc(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left + right + (cc.c ? 1 : 0));
    cc.c = (result & 0x100) != 0;
    cc.h = ((left ^ right ^ result) & 0x10) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint8_t M6809::op_or(uint8_t left, uint8_t right) {
    uint8_t result = uint8_t(left | right);
    cc.n = (result & 0x80) != 0;
    cc.z = result == 0;
    cc.v = false;
    return result;
}

uint8_t M6809::op_add8(uint8_t left, uint8_t right) {
    uint16_t result = uint16_t(left + right);
    cc.c = (result & 0x100) != 0;
    cc.h = ((left ^ right ^ result) & 0x10) != 0;
    cc.n = (result & 0x80) != 0;
    cc.z = (result & 0xff) == 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return uint8_t(result);
}

uint16_t M6809::op_add16(uint16_t left, uint16_t right) {
    uint32_t result = uint32_t(left) + uint32_t(right);
    cc.c = (result & 0x10000) != 0;
    cc.n = (result & 0x8000) != 0;
    cc.z = (result & 0xffff) == 0;
    cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x8000) != 0;
    return uint16_t(result);
}

uint8_t M6809::op_ld_st8(uint8_t value) {
    cc.n = (value & 0x80) != 0;
    cc.z = value == 0;
    cc.v = ((value >> 1) & 0x80) != 0;
    return value;
}

uint16_t M6809::op_ld_st16(uint16_t value) {
    cc.n = (value & 0x8000) != 0;
    cc.z = value == 0;
    cc.v = ((value >> 1) & 0x8000) != 0;
    return value;
}

void M6809::branch(bool condition, uint8_t offset) {
    if (condition) pc_ = uint16_t(pc_ + int8_t(offset));
}

void M6809::long_branch(bool condition, uint16_t offset) {
    if (condition) {
        pc_ = uint16_t(pc_ + int16_t(offset));
        extra_cycles_ += 1;
    }
}

void M6809::page_10(uint8_t opcode) {
    switch (opcode) {
        case 0x22: long_branch(!(cc.c || cc.z), address_); break;            // lbhi
        case 0x23: long_branch(cc.c || cc.z, address_); break;               // lbls
        case 0x24: long_branch(!cc.c, address_); break;                      // lbcc
        case 0x25: long_branch(cc.c, address_); break;                       // lbcs
        case 0x26: long_branch(!cc.z, address_); break;                      // lbne
        case 0x27: long_branch(cc.z, address_); break;                       // lbeq
        case 0x28: long_branch(!cc.v, address_); break;                      // lbvc
        case 0x29: long_branch(cc.v, address_); break;                       // lbvs
        case 0x2a: long_branch(!cc.n, address_); break;                      // lbpl
        case 0x2b: long_branch(cc.n, address_); break;                       // lbmi
        case 0x2c: long_branch(cc.n == cc.v, address_); break;               // lbge
        case 0x2d: long_branch(cc.n != cc.v, address_); break;               // lblt
        case 0x2e: long_branch(cc.n == cc.v && !cc.z, address_); break;      // lbgt
        case 0x2f: long_branch(!(cc.n == cc.v && !cc.z), address_); break;   // lble
        case 0x83:
        case 0x93:
        case 0xa3:
        case 0xb3: op_sub16(d(), address_); break;  // cmpd
        case 0x8c:
        case 0x9c:
        case 0xac:
        case 0xbc: op_sub16(y, address_); break;  // cmpy
        case 0x8e:
        case 0x9e:
        case 0xae:
        case 0xbe: y = op_ld_st16(address_); break;  // ldy
        case 0x9f:
        case 0xaf:
        case 0xbf: write_word(address_, op_ld_st16(y)); break;  // sty
        case 0xce:
        case 0xde:
        case 0xee:
        case 0xfe:  // lds
            s = op_ld_st16(address_);
            stack_init_ = true;
            break;
        case 0xdf:
        case 0xef:
        case 0xff: write_word(address_, op_ld_st16(s)); break;  // sts
        default: break;
    }
}

void M6809::page_11(uint8_t opcode) {
    switch (opcode) {
        case 0x83:
        case 0x93:
        case 0xa3:
        case 0xb3: op_sub16(u, address_); break;  // cmpu
        case 0x8c:
        case 0x9c:
        case 0xac:
        case 0xbc: op_sub16(s, address_); break;  // cmps
        default: break;
    }
}

int M6809::run(int cycles) {
    int executed = 0;
    while (executed < cycles) {
        extra_cycles_ = 0;

        if (nmi_request_ != IrqLine::Clear) {
            extra_cycles_ = call_nmi();
        } else if (firq_state_ != IrqLine::Clear && !cc.f) {
            extra_cycles_ = call_firq();
        } else if (irq_state_ != IrqLine::Clear && !cc.i) {
            extra_cycles_ = call_irq();
        }

        if (cwai_) {
            // Waiting for an interrupt: burn the rest of the slice.
            int idle = cycles - executed;
            if (cycle_handler_) cycle_handler_(idle);
            return cycles;
        }

        uint8_t opcode = fetch_opcode();
        switch (kMode[opcode]) {
            case 0: break;  // implied
            case 1: address_ = uint16_t((dp << 8) | fetch()); break;
            case 2: operand_ = fetch(); break;
            case 3: address_ = fetch_word(); break;
            case 4: address_ = get_indexed(); break;
            case 5:
                address_ = uint16_t((dp << 8) | fetch());
                operand_ = read(address_);
                break;
            case 6:
                address_ = get_indexed();
                operand_ = read(address_);
                break;
            case 7:
                address_ = fetch_word();
                operand_ = read(address_);
                break;
            case 8:
                address_ = uint16_t((dp << 8) | fetch());
                address_ = read_word(address_);
                break;
            case 9: address_ = read_word(get_indexed()); break;
            case 10:
                address_ = fetch_word();
                address_ = read_word(address_);
                break;
            default: break;  // illegal opcode
        }

        switch (opcode) {
            case 0x00:
            case 0x01:
            case 0x60:
            case 0x70: write(address_, op_neg(read(address_))); break;
            case 0x02:
            case 0x03:
            case 0x63:
            case 0x73: write(address_, op_com(read(address_))); break;
            case 0x04:
            case 0x64:
            case 0x74: write(address_, op_lsr(read(address_))); break;
            case 0x06:
            case 0x66:
            case 0x76: write(address_, op_ror(read(address_))); break;
            case 0x07:
            case 0x67:
            case 0x77: write(address_, op_asr(read(address_))); break;
            case 0x08:
            case 0x68:
            case 0x78: write(address_, op_asl(read(address_))); break;
            case 0x09:
            case 0x69:
            case 0x79: write(address_, op_rol(read(address_))); break;
            case 0x0a:
            case 0x6a:
            case 0x7a: write(address_, op_dec(read(address_))); break;
            case 0x0c:
            case 0x6c:
            case 0x7c: write(address_, op_inc(read(address_))); break;
            case 0x0d:
            case 0x6d:
            case 0x7d: op_tst(read(address_)); break;
            case 0x0e:
            case 0x6e:
            case 0x7e: pc_ = address_; break;  // jmp
            case 0x0f:
            case 0x6f:
            case 0x7f:  // clr
                write(address_, 0);
                cc.n = false;
                cc.v = false;
                cc.c = false;
                cc.z = true;
                break;
            case 0x10:
            case 0x11: {
                uint8_t opcode2 = fetch_opcode();
                switch (kMode1X[opcode2]) {
                    case 1: address_ = uint16_t((dp << 8) | fetch()); break;
                    case 3: address_ = fetch_word(); break;
                    case 4: address_ = get_indexed(); break;
                    case 5:
                        address_ = uint16_t((dp << 8) | fetch());
                        address_ = read_word(address_);
                        break;
                    case 6: address_ = read_word(get_indexed()); break;
                    case 7:
                        address_ = fetch_word();
                        address_ = read_word(address_);
                        break;
                    default: break;
                }
                if (opcode == 0x10) {
                    page_10(opcode2);
                } else {
                    page_11(opcode2);
                }
                extra_cycles_ += kCycles1X[opcode2];
                break;
            }
            case 0x12: break;  // nop
            case 0x13:         // sync
                if (irq_state_ == IrqLine::Clear && firq_state_ == IrqLine::Clear &&
                    nmi_request_ == IrqLine::Clear) {
                    pc_ = uint16_t(pc_ - 1);
                }
                break;
            case 0x16: long_branch(true, address_); break;  // lbra
            case 0x17:                                      // lbsr
                push_sw(pc_);
                pc_ = uint16_t(pc_ + int16_t(address_));
                break;
            case 0x19: {  // daa
                uint8_t correction = 0;
                uint8_t high = uint8_t(a & 0xf0);
                uint8_t low = uint8_t(a & 0x0f);
                if (low > 0x09 || cc.h) correction = uint8_t(correction | 0x06);
                if (high > 0x80 && low > 0x09) correction = uint8_t(correction | 0x60);
                if (high > 0x90 || cc.c) correction = uint8_t(correction | 0x60);
                uint16_t result = uint16_t(correction + a);
                cc.v = false;
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.c = cc.c || (result & 0x100) != 0;
                a = uint8_t(result);
                break;
            }
            case 0x1a: set_cc(uint8_t(get_cc() | operand_)); break;   // orcc
            case 0x1c: set_cc(uint8_t(get_cc() & operand_)); break;   // andcc
            case 0x1d:                                                // sex
                a = uint8_t(0xff * (b >> 7));
                cc.n = (d() & 0x8000) != 0;
                cc.z = d() == 0;
                break;
            case 0x1e: exg(operand_); break;
            case 0x1f: tfr(operand_); break;
            case 0x20: branch(true, operand_); break;                              // bra
            case 0x21: break;                                                      // brn
            case 0x22: branch(!(cc.c || cc.z), operand_); break;                   // bhi
            case 0x23: branch(cc.c || cc.z, operand_); break;                      // bls
            case 0x24: branch(!cc.c, operand_); break;                             // bcc
            case 0x25: branch(cc.c, operand_); break;                              // bcs
            case 0x26: branch(!cc.z, operand_); break;                             // bne
            case 0x27: branch(cc.z, operand_); break;                              // beq
            case 0x28: branch(!cc.v, operand_); break;                             // bvc
            case 0x29: branch(cc.v, operand_); break;                              // bvs
            case 0x2a: branch(!cc.n, operand_); break;                             // bpl
            case 0x2b: branch(cc.n, operand_); break;                              // bmi
            case 0x2c: branch(cc.n == cc.v, operand_); break;                      // bge
            case 0x2d: branch(cc.n != cc.v, operand_); break;                      // blt
            case 0x2e: branch(cc.n == cc.v && !cc.z, operand_); break;             // bgt
            case 0x2f: branch(!(cc.n == cc.v && !cc.z), operand_); break;          // ble
            case 0x30:                                                             // leax
                x = address_;
                cc.z = x == 0;
                break;
            case 0x31:  // leay
                y = address_;
                cc.z = y == 0;
                break;
            case 0x32: s = address_; break;  // leas
            case 0x33: u = address_; break;  // leau
            case 0x34:                       // pshs
                if ((operand_ & 0x80) != 0) {
                    push_sw(pc_);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x40) != 0) {
                    push_sw(u);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x20) != 0) {
                    push_sw(y);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x10) != 0) {
                    push_sw(x);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x08) != 0) {
                    push_s(dp);
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x04) != 0) {
                    push_s(b);
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x02) != 0) {
                    push_s(a);
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x01) != 0) {
                    push_s(get_cc());
                    extra_cycles_ += 1;
                }
                break;
            case 0x35:  // puls
                if ((operand_ & 0x01) != 0) {
                    set_cc(pop_s());
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x02) != 0) {
                    a = pop_s();
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x04) != 0) {
                    b = pop_s();
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x08) != 0) {
                    dp = pop_s();
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x10) != 0) {
                    x = pop_sw();
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x20) != 0) {
                    y = pop_sw();
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x40) != 0) {
                    u = pop_sw();
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x80) != 0) {
                    pc_ = pop_sw();
                    extra_cycles_ += 2;
                }
                break;
            case 0x36:  // pshu
                if ((operand_ & 0x80) != 0) {
                    push_uw(pc_);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x40) != 0) {
                    push_uw(s);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x20) != 0) {
                    push_uw(y);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x10) != 0) {
                    push_uw(x);
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x08) != 0) {
                    push_u(dp);
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x04) != 0) {
                    push_u(b);
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x02) != 0) {
                    push_u(a);
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x01) != 0) {
                    push_u(get_cc());
                    extra_cycles_ += 1;
                }
                break;
            case 0x37:  // pulu
                if ((operand_ & 0x01) != 0) {
                    set_cc(pop_u());
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x02) != 0) {
                    a = pop_u();
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x04) != 0) {
                    b = pop_u();
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x08) != 0) {
                    dp = pop_u();
                    extra_cycles_ += 1;
                }
                if ((operand_ & 0x10) != 0) {
                    x = pop_uw();
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x20) != 0) {
                    y = pop_uw();
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x40) != 0) {
                    s = pop_uw();
                    extra_cycles_ += 2;
                }
                if ((operand_ & 0x80) != 0) {
                    pc_ = pop_uw();
                    extra_cycles_ += 2;
                }
                break;
            case 0x39: pc_ = pop_sw(); break;         // rts
            case 0x3a: x = uint16_t(x + b); break;    // abx
            case 0x3b:                                // rti
                set_cc(pop_s());
                if (cc.e) {
                    extra_cycles_ += 9;
                    a = pop_s();
                    b = pop_s();
                    dp = pop_s();
                    x = pop_sw();
                    y = pop_sw();
                    u = pop_sw();
                }
                pc_ = pop_sw();
                break;
            case 0x3c:  // cwai
                set_cc(uint8_t(get_cc() & operand_));
                cc.e = true;
                push_sw(pc_);
                push_sw(u);
                push_sw(y);
                push_sw(x);
                push_s(dp);
                push_s(b);
                push_s(a);
                push_s(get_cc());
                cwai_ = true;
                break;
            case 0x3d: {  // mul
                uint16_t result = uint16_t(uint16_t(a) * uint16_t(b));
                set_d(result);
                cc.c = (result & 0x80) != 0;
                cc.z = result == 0;
                break;
            }
            case 0x3f:  // swi
                cc.e = true;
                push_sw(pc_);
                push_sw(u);
                push_sw(y);
                push_sw(x);
                push_s(dp);
                push_s(b);
                push_s(a);
                push_s(get_cc());
                cc.i = true;
                cc.f = true;
                pc_ = read_word(0xfffa);
                break;
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
            case 0x80:
            case 0x90:
            case 0xa0:
            case 0xb0: a = op_sub8(a, operand_); break;  // suba
            case 0x81:
            case 0x91:
            case 0xa1:
            case 0xb1: op_sub8(a, operand_); break;  // cmpa
            case 0x82:
            case 0x92:
            case 0xa2:
            case 0xb2: a = op_sbc(a, operand_); break;  // sbca
            case 0x83:
            case 0x93:
            case 0xa3:
            case 0xb3: set_d(op_sub16(d(), address_)); break;  // subd
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
            case 0xb6: a = op_ld_st8(operand_); break;  // lda
            case 0x97:
            case 0xa7:
            case 0xb7: write(address_, op_ld_st8(a)); break;  // sta
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
            case 0xbb: a = op_add8(a, operand_); break;  // adda
            case 0x8c:
            case 0x9c:
            case 0xac:
            case 0xbc: op_sub16(x, address_); break;  // cmpx
            case 0x8d:                                // bsr
                push_sw(pc_);
                pc_ = uint16_t(pc_ + int8_t(operand_));
                break;
            case 0x9d:
            case 0xad:
            case 0xbd:  // jsr
                push_sw(pc_);
                pc_ = address_;
                break;
            case 0x8e:
            case 0x9e:
            case 0xae:
            case 0xbe: x = op_ld_st16(address_); break;  // ldx
            case 0x9f:
            case 0xaf:
            case 0xbf: write_word(address_, op_ld_st16(x)); break;  // stx
            case 0xc0:
            case 0xd0:
            case 0xe0:
            case 0xf0: b = op_sub8(b, operand_); break;  // subb
            case 0xc1:
            case 0xd1:
            case 0xe1:
            case 0xf1: op_sub8(b, operand_); break;  // cmpb
            case 0xc2:
            case 0xd2:
            case 0xe2:
            case 0xf2: b = op_sbc(b, operand_); break;  // sbcb
            case 0xc3:
            case 0xd3:
            case 0xe3:
            case 0xf3: set_d(op_add16(d(), address_)); break;  // addd
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
            case 0xf6: b = op_ld_st8(operand_); break;  // ldb
            case 0xd7:
            case 0xe7:
            case 0xf7: write(address_, op_ld_st8(b)); break;  // stb
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
            case 0xfb: b = op_add8(b, operand_); break;  // addb
            case 0xcc:
            case 0xdc:
            case 0xec:
            case 0xfc: set_d(op_ld_st16(address_)); break;  // ldd
            case 0xdd:
            case 0xed:
            case 0xfd: write_word(address_, op_ld_st16(d())); break;  // std
            case 0xce:
            case 0xde:
            case 0xee:
            case 0xfe: u = op_ld_st16(address_); break;  // ldu
            case 0xdf:
            case 0xef:
            case 0xff: write_word(address_, op_ld_st16(u)); break;  // stu
            default: break;
        }

        // Illegal opcodes have no entry in the cycle table: charge one cycle so
        // that the slice always makes progress.
        int elapsed = kCycles[opcode] + extra_cycles_;
        if (elapsed <= 0) elapsed = 1;
        executed += elapsed;
        if (cycle_handler_) cycle_handler_(elapsed);
    }
    return executed;
}

}  // namespace dsp
