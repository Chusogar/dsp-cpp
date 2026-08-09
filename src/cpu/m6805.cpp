#include "cpu/m6805.h"

namespace dsp {
namespace {

// Cycles per opcode, ciclos_6805[] in m6805.pas with the gaps of the opcodes
// the original does not decode filled in from their row.
constexpr uint8_t kCycles[256] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  //
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,   //
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   //
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,   //
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   //
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   //
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,   //
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,   //
    9,  6,  2,  11, 2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,   //
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,   //
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  8,  2,  2,   //
    4,  4,  4,  4,  4,  4,  4,  5,  4,  4,  4,  4,  3,  7,  4,  5,   //
    5,  5,  5,  5,  5,  5,  5,  6,  5,  5,  5,  5,  4,  8,  5,  6,   //
    6,  6,  6,  6,  6,  6,  6,  7,  6,  6,  6,  6,  5,  9,  6,  7,   //
    5,  5,  5,  5,  5,  5,  5,  6,  5,  5,  5,  5,  4,  8,  5,  6,   //
    4,  4,  4,  4,  4,  4,  4,  5,  4,  4,  4,  4,  3,  7,  4,  5,
};

// Opcodes that only need the effective address: reading the operand would
// disturb the I/O registers the MCU maps at the bottom of its address space.
bool writes_only(uint8_t opcode) {
    switch (opcode & 0x0f) {
        case 0x07:  // sta
        case 0x0c:  // jmp
        case 0x0d:  // jsr
        case 0x0f:  // stx
            return opcode >= 0xb0;
        default:
            return false;
    }
}

}  // namespace

M6805::M6805(uint32_t clock, Type type) : clock_(clock / 4), type_(type) {
    read_ = [](uint16_t) { return uint8_t(0xff); };
    write_ = [](uint16_t, uint8_t) {};
}

void M6805::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void M6805::reset() {
    cc = Flags{};
    a = 0;
    x = 0;
    sp_mask_ = 0x7f;
    sp_low_ = 0x60;
    sp = sp_mask_;
    irq_state_ = IrqLine::Clear;
    irq_pending_ = false;
    pc_ = read_word(0xfffe);
}

void M6805::set_irq(IrqLine state) {
    irq_state_ = state;
    if (state != IrqLine::Clear) irq_pending_ = true;
}

uint16_t M6805::read_word(uint16_t address) {
    return uint16_t((uint16_t(read(address)) << 8) | read(uint16_t(address + 1)));
}

uint8_t M6805::fetch() {
    return read(pc_++);
}

uint16_t M6805::fetch_word() {
    const uint16_t value = read_word(pc_);
    pc_ = uint16_t(pc_ + 2);
    return value;
}

uint8_t M6805::get_cc() const {
    return uint8_t((cc.h ? 0x10 : 0) | (cc.i ? 0x08 : 0) | (cc.n ? 0x04 : 0) | (cc.z ? 0x02 : 0) |
                   (cc.c ? 0x01 : 0));
}

void M6805::set_cc(uint8_t value) {
    cc.h = (value & 0x10) != 0;
    cc.i = (value & 0x08) != 0;
    cc.n = (value & 0x04) != 0;
    cc.z = (value & 0x02) != 0;
    cc.c = (value & 0x01) != 0;
}

void M6805::push(uint8_t value) {
    write(sp, value);
    sp = sp == sp_low_ ? sp_mask_ : uint16_t(sp - 1);
}

void M6805::push_word(uint16_t value) {
    push(uint8_t(value));
    push(uint8_t(value >> 8));
}

uint8_t M6805::pull() {
    sp = sp >= sp_mask_ ? sp_low_ : uint16_t(sp + 1);
    return read(sp);
}

uint16_t M6805::pull_word() {
    const uint8_t high = pull();
    const uint8_t low = pull();
    return uint16_t((uint16_t(high) << 8) | low);
}

void M6805::branch(bool taken, uint8_t offset) {
    if (taken) pc_ = uint16_t(pc_ + int8_t(offset));
}

int M6805::run(int cycles) {
    int elapsed = 0;
    while (elapsed < cycles) {
        int extra = 0;
        if (type_ == Type::M68705 && irq_pending_ && !cc.i) {
            push_word(pc_);
            push(x);
            push(a);
            push(get_cc());
            cc.i = true;
            irq_pending_ = false;
            pc_ = read_word(0xfffa);
            extra = 11;
        }
        const int used = execute() + extra;
        elapsed += used;
        if (cycle_handler_) cycle_handler_(used);
    }
    return elapsed;
}

int M6805::execute() {
    const uint8_t opcode = fetch();
    uint16_t address = 0;
    uint8_t value = 0;
    bool has_address = false;

    // The addressing mode of the 6805 only depends on the high nibble.
    switch (opcode >> 4) {
        case 0x0:  // bit test and branch: direct operand plus a relative offset
        case 0x1:  // bit set/clear: direct
        case 0x3:  // read/modify/write: direct
        case 0xb:  // direct
            address = fetch();
            has_address = true;
            break;
        case 0x2:  // relative
        case 0xa:  // immediate
            value = fetch();
            break;
        case 0x6:  // indexed, 8 bit offset
        case 0xe:
            address = uint16_t(fetch() + x);
            has_address = true;
            break;
        case 0x7:  // indexed, no offset
        case 0xf:
            address = x;
            has_address = true;
            break;
        case 0xc:  // extended
            address = fetch_word();
            has_address = true;
            break;
        case 0xd:  // indexed, 16 bit offset
            address = uint16_t(fetch_word() + x);
            has_address = true;
            break;
        default:
            break;  // inherent
    }
    if (has_address && !writes_only(opcode)) value = read(address);

    auto set_nz = [this](uint8_t result) {
        cc.z = result == 0;
        cc.n = (result & 0x80) != 0;
    };
    auto set_nzc = [this](uint16_t result) {
        cc.z = (result & 0xff) == 0;
        cc.n = (result & 0x80) != 0;
        cc.c = (result & 0x100) != 0;
    };

    switch (opcode) {
        // Bit test and branch.
        case 0x00: case 0x02: case 0x04: case 0x06:
        case 0x08: case 0x0a: case 0x0c: case 0x0e: {  // brset
            const uint8_t offset = fetch();
            cc.c = (value & (1 << ((opcode >> 1) & 7))) != 0;
            branch(cc.c, offset);
            break;
        }
        case 0x01: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0b: case 0x0d: case 0x0f: {  // brclr
            const uint8_t offset = fetch();
            cc.c = (value & (1 << ((opcode >> 1) & 7))) != 0;
            branch(!cc.c, offset);
            break;
        }
        case 0x10: case 0x12: case 0x14: case 0x16:
        case 0x18: case 0x1a: case 0x1c: case 0x1e:  // bset
            write(address, uint8_t(value | (1 << ((opcode >> 1) & 7))));
            break;
        case 0x11: case 0x13: case 0x15: case 0x17:
        case 0x19: case 0x1b: case 0x1d: case 0x1f:  // bclr
            write(address, uint8_t(value & ~(1 << ((opcode >> 1) & 7))));
            break;

        // Branches.
        case 0x20: branch(true, value); break;                    // bra
        case 0x21: break;                                          // brn
        case 0x22: branch(!cc.z && !cc.c, value); break;            // bhi
        case 0x23: branch(cc.z || cc.c, value); break;              // bls
        case 0x24: branch(!cc.c, value); break;                     // bcc
        case 0x25: branch(cc.c, value); break;                      // bcs
        case 0x26: branch(!cc.z, value); break;                     // bne
        case 0x27: branch(cc.z, value); break;                      // beq
        case 0x28: branch(!cc.h, value); break;                     // bhcc
        case 0x29: branch(cc.h, value); break;                      // bhcs
        case 0x2a: branch(!cc.n, value); break;                     // bpl
        case 0x2b: branch(cc.n, value); break;                      // bmi
        case 0x2c: branch(!cc.i, value); break;                     // bmc
        case 0x2d: branch(cc.i, value); break;                      // bms
        case 0x2e: branch(irq_state_ != IrqLine::Clear, value); break;  // bil
        case 0x2f: branch(irq_state_ == IrqLine::Clear, value); break;  // bih

        // Read/modify/write on memory.
        case 0x30: case 0x60: case 0x70: {  // neg
            const uint16_t result = uint16_t(0u - value);
            set_nzc(result);
            write(address, uint8_t(result));
            break;
        }
        case 0x33: case 0x63: case 0x73: {  // com
            const uint8_t result = uint8_t(~value);
            set_nz(result);
            cc.c = true;
            write(address, result);
            break;
        }
        case 0x34: case 0x64: case 0x74: {  // lsr
            cc.c = (value & 0x01) != 0;
            const uint8_t result = uint8_t(value >> 1);
            cc.n = false;
            cc.z = result == 0;
            write(address, result);
            break;
        }
        case 0x36: case 0x66: case 0x76: {  // ror
            const uint8_t result = uint8_t((cc.c ? 0x80 : 0) | (value >> 1));
            cc.c = (value & 0x01) != 0;
            set_nz(result);
            write(address, result);
            break;
        }
        case 0x37: case 0x67: case 0x77: {  // asr
            cc.c = (value & 0x01) != 0;
            const uint8_t result = uint8_t((value & 0x80) | (value >> 1));
            set_nz(result);
            write(address, result);
            break;
        }
        case 0x38: case 0x68: case 0x78: {  // asl / lsl
            const uint16_t result = uint16_t(value << 1);
            set_nzc(result);
            write(address, uint8_t(result));
            break;
        }
        case 0x39: case 0x69: case 0x79: {  // rol
            const uint16_t result = uint16_t((value << 1) | (cc.c ? 1 : 0));
            set_nzc(result);
            write(address, uint8_t(result));
            break;
        }
        case 0x3a: case 0x6a: case 0x7a: {  // dec
            const uint8_t result = uint8_t(value - 1);
            set_nz(result);
            write(address, result);
            break;
        }
        case 0x3c: case 0x6c: case 0x7c: {  // inc
            const uint8_t result = uint8_t(value + 1);
            set_nz(result);
            write(address, result);
            break;
        }
        case 0x3d: case 0x6d: case 0x7d:  // tst
            set_nz(value);
            break;
        case 0x3f: case 0x6f: case 0x7f:  // clr
            cc.n = false;
            cc.c = false;
            cc.z = true;
            write(address, 0);
            break;

        // Accumulator and index register.
        case 0x40: {  // nega
            const uint16_t result = uint16_t(0u - a);
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0x43:  // coma
            a = uint8_t(~a);
            set_nz(a);
            cc.c = true;
            break;
        case 0x44:  // lsra
            cc.c = (a & 0x01) != 0;
            a = uint8_t(a >> 1);
            cc.n = false;
            cc.z = a == 0;
            break;
        case 0x46:  // rora
            {
                const uint8_t result = uint8_t((cc.c ? 0x80 : 0) | (a >> 1));
                cc.c = (a & 0x01) != 0;
                a = result;
                set_nz(a);
            }
            break;
        case 0x47:  // asra
            cc.c = (a & 0x01) != 0;
            a = uint8_t((a & 0x80) | (a >> 1));
            set_nz(a);
            break;
        case 0x48: {  // lsla
            const uint16_t result = uint16_t(a << 1);
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0x49: {  // rola
            const uint16_t result = uint16_t((a << 1) | (cc.c ? 1 : 0));
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0x4a:  // deca
            a = uint8_t(a - 1);
            set_nz(a);
            break;
        case 0x4c:  // inca
            a = uint8_t(a + 1);
            set_nz(a);
            break;
        case 0x4d:  // tsta
            set_nz(a);
            break;
        case 0x4f:  // clra
            a = 0;
            cc.n = false;
            cc.z = true;
            break;
        case 0x50: {  // negx
            const uint16_t result = uint16_t(0u - x);
            set_nzc(result);
            x = uint8_t(result);
            break;
        }
        case 0x53:  // comx
            x = uint8_t(~x);
            set_nz(x);
            cc.c = true;
            break;
        case 0x54:  // lsrx
            cc.c = (x & 0x01) != 0;
            x = uint8_t(x >> 1);
            cc.n = false;
            cc.z = x == 0;
            break;
        case 0x56: {  // rorx
            const uint8_t result = uint8_t((cc.c ? 0x80 : 0) | (x >> 1));
            cc.c = (x & 0x01) != 0;
            x = result;
            set_nz(x);
            break;
        }
        case 0x57:  // asrx
            cc.c = (x & 0x01) != 0;
            x = uint8_t((x & 0x80) | (x >> 1));
            set_nz(x);
            break;
        case 0x58: {  // aslx
            const uint16_t result = uint16_t(x << 1);
            set_nzc(result);
            x = uint8_t(result);
            break;
        }
        case 0x59: {  // rolx
            const uint16_t result = uint16_t((x << 1) | (cc.c ? 1 : 0));
            set_nzc(result);
            x = uint8_t(result);
            break;
        }
        case 0x5a:  // decx
            x = uint8_t(x - 1);
            set_nz(x);
            break;
        case 0x5c:  // incx
            x = uint8_t(x + 1);
            set_nz(x);
            break;
        case 0x5d:  // tstx
            set_nz(x);
            break;
        case 0x5f:  // clrx
            x = 0;
            cc.n = false;
            cc.z = true;
            break;

        // Control.
        case 0x80:  // rti
            set_cc(pull());
            a = pull();
            x = pull();
            pc_ = pull_word();
            break;
        case 0x81:  // rts
            pc_ = pull_word();
            break;
        case 0x83:  // swi
            push_word(pc_);
            push(x);
            push(a);
            push(get_cc());
            cc.i = true;
            pc_ = read_word(0xfffc);
            break;
        case 0x97: x = a; break;         // tax
        case 0x98: cc.c = false; break;  // clc
        case 0x99: cc.c = true; break;   // sec
        case 0x9a: cc.i = false; break;  // cli
        case 0x9b: cc.i = true; break;   // sei
        case 0x9c: sp = sp_mask_; break;  // rsp
        case 0x9d: break;                 // nop
        case 0x9f: a = x; break;          // txa

        // ALU on the accumulator.
        case 0xa0: case 0xb0: case 0xc0: case 0xd0: case 0xe0: case 0xf0: {  // sub
            const uint16_t result = uint16_t(a - value);
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0xa1: case 0xb1: case 0xc1: case 0xd1: case 0xe1: case 0xf1:  // cmp
            set_nzc(uint16_t(a - value));
            break;
        case 0xa2: case 0xb2: case 0xc2: case 0xd2: case 0xe2: case 0xf2: {  // sbc
            const uint16_t result = uint16_t(a - value - (cc.c ? 1 : 0));
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0xa3: case 0xb3: case 0xc3: case 0xd3: case 0xe3: case 0xf3:  // cpx
            set_nzc(uint16_t(x - value));
            break;
        case 0xa4: case 0xb4: case 0xc4: case 0xd4: case 0xe4: case 0xf4:  // and
            a = uint8_t(a & value);
            set_nz(a);
            break;
        case 0xa5: case 0xb5: case 0xc5: case 0xd5: case 0xe5: case 0xf5:  // bit
            set_nz(uint8_t(a & value));
            break;
        case 0xa6: case 0xb6: case 0xc6: case 0xd6: case 0xe6: case 0xf6:  // lda
            a = value;
            set_nz(a);
            break;
        case 0xb7: case 0xc7: case 0xd7: case 0xe7: case 0xf7:  // sta
            set_nz(a);
            write(address, a);
            break;
        case 0xa8: case 0xb8: case 0xc8: case 0xd8: case 0xe8: case 0xf8:  // eor
            a = uint8_t(a ^ value);
            set_nz(a);
            break;
        case 0xa9: case 0xb9: case 0xc9: case 0xd9: case 0xe9: case 0xf9: {  // adc
            const uint16_t result = uint16_t(a + value + (cc.c ? 1 : 0));
            cc.h = ((a ^ value ^ result) & 0x10) != 0;
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0xaa: case 0xba: case 0xca: case 0xda: case 0xea: case 0xfa:  // ora
            a = uint8_t(a | value);
            set_nz(a);
            break;
        case 0xab: case 0xbb: case 0xcb: case 0xdb: case 0xeb: case 0xfb: {  // add
            const uint16_t result = uint16_t(a + value);
            cc.h = ((a ^ value ^ result) & 0x10) != 0;
            set_nzc(result);
            a = uint8_t(result);
            break;
        }
        case 0xbc: case 0xcc: case 0xdc: case 0xec: case 0xfc:  // jmp
            pc_ = address;
            break;
        case 0xad:  // bsr
            push_word(pc_);
            pc_ = uint16_t(pc_ + int8_t(value));
            break;
        case 0xbd: case 0xcd: case 0xdd: case 0xed: case 0xfd:  // jsr
            push_word(pc_);
            pc_ = address;
            break;
        case 0xae: case 0xbe: case 0xce: case 0xde: case 0xee: case 0xfe:  // ldx
            x = value;
            set_nz(x);
            break;
        case 0xbf: case 0xcf: case 0xdf: case 0xef: case 0xff:  // stx
            set_nz(x);
            write(address, x);
            break;
        default:
            break;  // illegal opcodes behave as a nop
    }

    return kCycles[opcode];
}

}  // namespace dsp
