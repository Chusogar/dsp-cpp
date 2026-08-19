#include "cpu/m68000.h"

#include <algorithm>

namespace dsp {
namespace {

// Cycle helpers from m68000.pas.
int calc_ea_t_bw(uint8_t dir) {
    if (dir <= 0x0f) return 0;
    if (dir <= 0x1f) return 4;
    if (dir <= 0x27) return 6;
    if (dir <= 0x2f) return 8;
    if (dir <= 0x37) return 10;
    switch (dir) {
        case 0x38: return 8;
        case 0x39: return 12;
        case 0x3a: return 8;
        case 0x3b: return 10;
        case 0x3c: return 4;
        default: return 0;
    }
}

int calc_ea_t_l(uint8_t dir) {
    if (dir <= 0x0f) return 0;
    if (dir <= 0x1f) return 8;
    if (dir <= 0x27) return 10;
    if (dir <= 0x2f) return 12;
    if (dir <= 0x37) return 14;
    switch (dir) {
        case 0x38: return 12;
        case 0x39: return 16;
        case 0x3a: return 12;
        case 0x3b: return 14;
        case 0x3c: return 8;
        default: return 0;
    }
}

int calc_move_t(uint8_t dir, uint8_t dest, bool long_size) {
    static const uint8_t bw[6][8] = {
        {4, 4, 8, 8, 8, 12, 14, 12},     {8, 8, 12, 12, 12, 16, 18, 16},
        {10, 10, 14, 14, 14, 18, 20, 18}, {12, 12, 16, 16, 16, 20, 22, 20},
        {14, 14, 18, 18, 18, 22, 24, 22}, {16, 16, 20, 20, 20, 24, 26, 24},
    };
    static const uint8_t lw[6][8] = {
        {4, 4, 12, 12, 12, 16, 18, 16},   {12, 12, 20, 20, 20, 24, 26, 24},
        {14, 14, 22, 22, 22, 26, 28, 26}, {16, 16, 24, 24, 24, 28, 30, 28},
        {18, 18, 26, 26, 26, 30, 32, 30}, {20, 20, 28, 28, 28, 32, 34, 32},
    };
    int row = 0;
    if (dir <= 0x0f) row = 0;
    else if (dir <= 0x1f || dir == 0x3c) row = 1;
    else if (dir <= 0x27) row = 2;
    else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) row = 3;
    else if (dir <= 0x37 || dir == 0x3b) row = 4;
    else if (dir == 0x39) row = 5;
    int res = long_size ? lw[row][dest >> 3] : bw[row][dest >> 3];
    if (dest == 39) res += 4;
    return res;
}

const uint8_t kShift8[65] = {
    0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff};

uint16_t shift16(int count) {
    if (count <= 0) return 0;
    if (count >= 16) return 0xffff;
    return uint16_t(0xffffu << (16 - count));
}

uint32_t shift32(int count) {
    if (count <= 0) return 0;
    if (count >= 32) return 0xffffffffu;
    return uint32_t(0xffffffffu << (32 - count));
}

}  // namespace

M68000::M68000(uint32_t clock, Type type) : clock_(clock), type_(type) {
    irq_.fill(IrqLine::Clear);
}

void M68000::set_memory_handlers(ReadWordHandler read, WriteWordHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void M68000::set_irq(int level, IrqLine state) {
    if (level >= 0 && level < 8) irq_[size_t(level)] = state;
}

void M68000::reset() {
    cc = Flags{};
    cc.s = true;
    cc.z = true;
    cc.im = 7;
    opcode_ = true;
    other_sp_ = Reg32{};
    a[7].set_wh(getword(0));
    a[7].set_wl(getword(2));
    pc_.set_wh(getword(4));
    pc_.set_wl(getword(6));
    irq_.fill(IrqLine::Clear);
    halt_request_ = IrqLine::Clear;
    reset_request_ = IrqLine::Clear;
    halted_ = false;
    stopped_ = false;
}

uint8_t M68000::getbyte(uint32_t address) {
    if (read_byte_) return read_byte_(address);
    uint16_t value = getword(address);
    return (address & 1) ? uint8_t(value & 0xff) : uint8_t(value >> 8);
}

void M68000::putbyte(uint32_t address, uint8_t value) {
    if (write_byte_) {
        write_byte_(address, value);
        return;
    }
    uint16_t old = getword(address);
    if (address & 1) putword(address, uint16_t((old & 0xff00) | value));
    else putword(address, uint16_t((old & 0x00ff) | (uint16_t(value) << 8)));
}

uint16_t M68000::fetch_word() {
    uint16_t value = getword(pc_.l);
    pc_.l += 2;
    return value;
}

uint32_t M68000::fetch_long() {
    uint32_t value = (uint32_t(getword(pc_.l)) << 16) | getword(pc_.l + 2);
    pc_.l += 4;
    return value;
}

uint16_t M68000::get_flags() const {
    uint16_t value = uint16_t(cc.t ? 0x8000 : 0);
    if (cc.s) value |= 0x2000;
    value |= uint16_t(cc.im << 8);
    if (cc.x) value |= 0x10;
    if (cc.n) value |= 0x08;
    if (cc.z) value |= 0x04;
    if (cc.v) value |= 0x02;
    if (cc.c) value |= 0x01;
    return value;
}

void M68000::set_flags(uint16_t value) {
    const bool supervisor = (value & 0x2000) != 0;
    if (cc.s != supervisor) {
        Reg32 current = a[7];
        a[7] = other_sp_;
        other_sp_ = current;
        cc.s = supervisor;
    }
    cc.t = (value & 0x8000) != 0;
    cc.im = uint8_t((value >> 8) & 7);
    cc.x = (value & 0x10) != 0;
    cc.n = (value & 0x08) != 0;
    cc.z = (value & 0x04) != 0;
    cc.v = (value & 0x02) != 0;
    cc.c = (value & 0x01) != 0;
}

uint32_t M68000::indexed_offset(uint32_t base) {
    const uint16_t extension = fetch_word();
    const int8_t displacement = int8_t(extension & 0xff);
    const size_t index = size_t((extension >> 12) & 7);
    const Reg32& reg = (extension & 0x8000) ? a[index] : d[index];
    const uint32_t value = (extension & 0x800) ? reg.l : uint32_t(int32_t(int16_t(reg.wl())));
    return base + value + uint32_t(int32_t(displacement));
}

uint8_t M68000::read_b(uint8_t dir) {
    if (dir <= 0x07) return d[dir & 7].l0();
    if (dir <= 0x0f) return a[dir & 7].l0();
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l = ea_ + ((dir & 7) == 7 ? 2 : 1);
    } else if (dir <= 0x27) {
        a[dir & 7].l -= ((dir & 7) == 7 ? 2 : 1);
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    } else if (dir == 0x3a) {
        ea_ = pc_.l + uint32_t(int32_t(int16_t(getword(pc_.l))));
        pc_.l += 2;
    } else if (dir == 0x3b) {
        ea_ = indexed_offset(pc_.l);
    } else if (dir == 0x3c) {
        ea_ = pc_.l;
        pc_.l += 2;
        return uint8_t(getword(ea_) & 0xff);
    }
    opcode_ = false;
    const uint8_t value = getbyte(ea_);
    opcode_ = true;
    return value;
}

void M68000::write_b2(uint8_t dir, uint8_t value) {
    if (dir <= 0x07) d[dir & 7].set_l0(value);
    else if (dir <= 0x0f) a[dir & 7].set_l0(value);
    else putbyte(ea_, value);
}

void M68000::write_b(uint8_t dir, uint8_t value) {
    if (dir <= 0x07) {
        d[dir & 7].set_l0(value);
        return;
    }
    if (dir <= 0x0f) {
        a[dir & 7].set_l0(value);
        return;
    }
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l += ((dir & 7) == 7 ? 2 : 1);
    } else if (dir <= 0x27) {
        a[dir & 7].l -= ((dir & 7) == 7 ? 2 : 1);
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    }
    putbyte(ea_, value);
}

uint16_t M68000::read_w(uint8_t dir) {
    if (dir <= 0x07) return d[dir & 7].wl();
    if (dir <= 0x0f) return a[dir & 7].wl();
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l = ea_ + 2;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 2;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    } else if (dir == 0x3a) {
        ea_ = pc_.l + uint32_t(int32_t(int16_t(getword(pc_.l))));
        pc_.l += 2;
    } else if (dir == 0x3b) {
        ea_ = indexed_offset(pc_.l);
    } else if (dir == 0x3c) {
        ea_ = pc_.l;
        pc_.l += 2;
        return getword(ea_);
    }
    opcode_ = false;
    const uint16_t value = getword(ea_);
    opcode_ = true;
    return value;
}

void M68000::write_w2(uint8_t dir, uint16_t value) {
    if (dir <= 0x07) d[dir & 7].set_wl(value);
    else if (dir <= 0x0f) a[dir & 7].set_wl(value);
    else putword(ea_, value);
}

void M68000::write_w(uint8_t dir, uint16_t value) {
    if (dir <= 0x07) {
        d[dir & 7].set_wl(value);
        return;
    }
    if (dir <= 0x0f) {
        a[dir & 7].set_wl(value);
        return;
    }
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l += 2;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 2;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    }
    putword(ea_, value);
}

uint32_t M68000::read_l(uint8_t dir) {
    if (dir <= 0x07) return d[dir & 7].l;
    if (dir <= 0x0f) return a[dir & 7].l;
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l += 4;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 4;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    } else if (dir == 0x3a) {
        ea_ = pc_.l + uint32_t(int32_t(int16_t(getword(pc_.l))));
        pc_.l += 2;
    } else if (dir == 0x3b) {
        ea_ = indexed_offset(pc_.l);
    } else if (dir == 0x3c) {
        ea_ = pc_.l;
        return fetch_long();
    }
    opcode_ = false;
    const uint32_t value = (uint32_t(getword(ea_)) << 16) | getword(ea_ + 2);
    opcode_ = true;
    return value;
}

void M68000::write_l2(uint8_t dir, uint32_t value) {
    if (dir <= 0x07) {
        d[dir & 7].l = value;
    } else if (dir <= 0x0f) {
        a[dir & 7].l = value;
    } else {
        putword(ea_, uint16_t(value >> 16));
        putword(ea_ + 2, uint16_t(value));
    }
}

void M68000::write_l(uint8_t dir, uint32_t value) {
    if (dir <= 0x07) {
        d[dir & 7].l = value;
        return;
    }
    if (dir <= 0x0f) {
        a[dir & 7].l = value;
        return;
    }
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l = ea_ + 4;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 4;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = uint32_t(int32_t(int16_t(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    }
    putword(ea_, uint16_t(value >> 16));
    putword(ea_ + 2, uint16_t(value));
}

uint32_t M68000::read_ea(uint8_t dir) {
    if (dir >= 0x10 && dir <= 0x27) return a[dir & 7].l;
    if (dir >= 0x28 && dir <= 0x2f) {
        return a[dir & 7].l + uint32_t(int32_t(int16_t(fetch_word())));
    }
    if (dir >= 0x30 && dir <= 0x37) return indexed_offset(a[dir & 7].l);
    switch (dir) {
        case 0x38: return uint32_t(int32_t(int16_t(fetch_word())));
        case 0x39: return fetch_long();
        case 0x3a: {
            const uint32_t base = pc_.l;
            return base + uint32_t(int32_t(int16_t(fetch_word())));
        }
        case 0x3b: return indexed_offset(pc_.l);
        default: return 0;
    }
}

bool M68000::condition(uint8_t code) const {
    switch (code & 0x0f) {
        case 0x00: return true;
        case 0x01: return false;
        case 0x02: return !cc.c && !cc.z;
        case 0x03: return cc.c || cc.z;
        case 0x04: return !cc.c;
        case 0x05: return cc.c;
        case 0x06: return !cc.z;
        case 0x07: return cc.z;
        case 0x08: return !cc.v;
        case 0x09: return cc.v;
        case 0x0a: return !cc.n;
        case 0x0b: return cc.n;
        case 0x0c: return cc.n == cc.v;
        case 0x0d: return cc.n != cc.v;
        case 0x0e: return (cc.n == cc.v) && !cc.z;
        default: return (cc.n != cc.v) || cc.z;
    }
}

void M68000::exception(uint32_t vector, int cycles) {
    cycles_ += cycles;
    const uint16_t flags = get_flags();
    set_flags(uint16_t(flags | 0x2000));
    cc.t = false;
    a[7].l -= 6;
    putword(a[7].l, flags);
    putword(a[7].l + 2, pc_.wh());
    putword(a[7].l + 4, pc_.wl());
    opcode_ = false;
    pc_.set_wh(getword(vector));
    pc_.set_wl(getword(vector + 2));
    opcode_ = true;
}

bool M68000::check_supervisor() {
    if (cc.s) return true;
    // Privilege violation, vector 8.
    pc_.l = ppc_.l;
    exception(0x20, 34);
    return false;
}

void M68000::group_0(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);
    const uint8_t op = uint8_t((instruction >> 6) & 0x3f);

    switch (op) {
        case 0x00: {  // ori.b
            const uint8_t immediate = uint8_t(fetch_word());
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint8_t result = uint8_t(read_b(dir) | immediate);
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            } else {  // ori to ccr
                cycles_ += 20;
                const uint8_t result = uint8_t((get_flags() & 0xff) | immediate);
                cc.x = (result & 0x10) != 0;
                cc.n = (result & 0x08) != 0;
                cc.z = (result & 0x04) != 0;
                cc.v = (result & 0x02) != 0;
                cc.c = (result & 0x01) != 0;
            }
            break;
        }
        case 0x01: {  // ori.w
            const uint16_t immediate = fetch_word();
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint16_t result = uint16_t(read_w(dir) | immediate);
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            } else if (check_supervisor()) {  // ori to sr
                cycles_ += 20;
                set_flags(uint16_t(get_flags() | immediate));
            }
            break;
        }
        case 0x02: {  // ori.l
            cycles_ += (dir >> 3) != 0 ? 20 + calc_ea_t_l(dir) : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t result = read_l(dir) | immediate;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x04: case 0x0c: case 0x14: case 0x1c:
        case 0x24: case 0x2c: case 0x34: case 0x3c: {  // btst dynamic / movep.w er
            if ((dir >> 3) == 0) {
                cycles_ += 6;
                const uint32_t mask = 1u << (d[dest].l0() & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
            } else if ((dir >> 3) == 1) {  // movep.w memory to register
                cycles_ += 16;
                const uint32_t address = a[orig].l + fetch_word();
                d[dest].l = (d[dest].l & 0xffff0000u) | (uint32_t(getbyte(address)) << 8) |
                            getbyte(address + 2);
            } else {
                cycles_ += 4 + calc_ea_t_bw(dir);
                const uint8_t mask = uint8_t(1u << (d[dest].l0() & 7));
                cc.z = (read_b(dir) & mask) == 0;
            }
            break;
        }
        case 0x05: case 0x0d: case 0x15: case 0x1d:
        case 0x25: case 0x2d: case 0x35: case 0x3d: {  // bchg dynamic / movep.l er
            if ((dir >> 3) == 0) {
                cycles_ += 8;
                const uint32_t mask = 1u << (d[dest].l0() & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
                d[orig].l ^= mask;
            } else if ((dir >> 3) == 1) {  // movep.l memory to register
                cycles_ += 24;
                const uint32_t address = a[orig].l + fetch_word();
                d[dest].l = (uint32_t(getbyte(address)) << 24) |
                            (uint32_t(getbyte(address + 2)) << 16) |
                            (uint32_t(getbyte(address + 4)) << 8) | getbyte(address + 6);
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t mask = uint8_t(1u << (d[dest].l0() & 7));
                uint8_t value = read_b(dir);
                cc.z = (value & mask) == 0;
                write_b2(dir, uint8_t(value ^ mask));
            }
            break;
        }
        case 0x06: case 0x0e: case 0x16: case 0x1e:
        case 0x26: case 0x2e: case 0x36: case 0x3e: {  // bclr dynamic / movep.w re
            if ((dir >> 3) == 0) {
                cycles_ += 10;
                const uint32_t mask = 1u << (d[dest].l0() & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
                d[orig].l &= ~mask;
            } else if ((dir >> 3) == 1) {  // movep.w register to memory
                cycles_ += 16;
                const uint32_t address = a[orig].l + fetch_word();
                putbyte(address, uint8_t(d[dest].l >> 8));
                putbyte(address + 2, d[dest].l0());
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                if (type_ == Type::M68010) cycles_ += 2;
                const uint8_t mask = uint8_t(1u << (d[dest].l0() & 7));
                uint8_t value = read_b(dir);
                cc.z = (value & mask) == 0;
                write_b2(dir, uint8_t(value & ~mask));
            }
            break;
        }
        case 0x07: case 0x0f: case 0x17: case 0x1f:
        case 0x27: case 0x2f: case 0x37: case 0x3f: {  // bset dynamic / movep.l re
            if ((dir >> 3) == 0) {
                cycles_ += 8;
                const uint32_t mask = 1u << (d[dest].l0() & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
                d[orig].l |= mask;
            } else if ((dir >> 3) == 1) {  // movep.l register to memory
                cycles_ += 24;
                const uint32_t address = a[orig].l + uint32_t(int32_t(int16_t(fetch_word())));
                putbyte(address, uint8_t(d[dest].l >> 24));
                putbyte(address + 2, uint8_t(d[dest].l >> 16));
                putbyte(address + 4, uint8_t(d[dest].l >> 8));
                putbyte(address + 6, d[dest].l0());
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t mask = uint8_t(1u << (d[dest].l0() & 7));
                uint8_t value = read_b(dir);
                cc.z = (value & mask) == 0;
                write_b2(dir, uint8_t(value | mask));
            }
            break;
        }
        case 0x08: {  // andi.b
            const uint8_t immediate = uint8_t(fetch_word());
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint8_t result = uint8_t(read_b(dir) & immediate);
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            } else {  // andi to ccr
                cycles_ += type_ == Type::M68010 ? 16 : 20;
                const uint8_t result = uint8_t((get_flags() & 0xff) & immediate);
                cc.x = (result & 0x10) != 0;
                cc.n = (result & 0x08) != 0;
                cc.z = (result & 0x04) != 0;
                cc.v = (result & 0x02) != 0;
                cc.c = (result & 0x01) != 0;
            }
            break;
        }
        case 0x09: {  // andi.w
            const uint16_t immediate = fetch_word();
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint16_t result = uint16_t(read_w(dir) & immediate);
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            } else if (check_supervisor()) {  // andi to sr
                cycles_ += type_ == Type::M68010 ? 16 : 20;
                set_flags(uint16_t(get_flags() & immediate));
            }
            break;
        }
        case 0x0a: {  // andi.l
            cycles_ += (dir >> 3) != 0 ? 20 + calc_ea_t_l(dir) : 14;
            const uint32_t immediate = fetch_long();
            const uint32_t result = read_l(dir) & immediate;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = false;
            cc.c = false;
            break;
        }
        case 0x10: {  // subi.b
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint8_t immediate = uint8_t(fetch_word() & 0xff);
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(value - immediate);
            write_b2(dir, uint8_t(result));
            cc.v = (((immediate ^ value) & (result ^ value)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            break;
        }
        case 0x11: {  // subi.w
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint16_t immediate = fetch_word();
            const uint16_t value = read_w(dir);
            const uint32_t result = uint32_t(value) - immediate;
            write_w2(dir, uint16_t(result));
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x12: {  // subi.l
            if ((dir >> 3) != 0) cycles_ += 20 + calc_ea_t_l(dir);
            else cycles_ += type_ == Type::M68010 ? 14 : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t value = read_l(dir);
            const uint32_t result = value - immediate;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
            cc.c = ((((immediate & result) | (~value & (immediate | result))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x18: {  // addi.b
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint8_t immediate = uint8_t(fetch_word() & 0xff);
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(immediate + value);
            write_b2(dir, uint8_t(result));
            cc.v = (((immediate ^ result) & (value ^ result)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            break;
        }
        case 0x19: {  // addi.w
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint16_t immediate = fetch_word();
            const uint16_t value = read_w(dir);
            const uint32_t result = uint32_t(immediate) + value;
            write_w2(dir, uint16_t(result));
            cc.v = ((((immediate ^ result) & (value ^ result)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x1a: {  // addi.l
            if ((dir >> 3) != 0) cycles_ += 20 + calc_ea_t_l(dir);
            else cycles_ += type_ == Type::M68010 ? 14 : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t value = read_l(dir);
            const uint32_t result = immediate + value;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((immediate ^ result) & (value ^ result)) >> 24) & 0x80) != 0;
            cc.c = ((((immediate & value) | (~result & (immediate | value))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x20: {  // btst static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 10;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                cc.z = ((read_b(dir) >> (bit & 7)) & 1) == 0;
            }
            break;
        }
        case 0x21: {  // bchg static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 12;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
                d[orig].l ^= 1u << (bit & 0x1f);
            } else {
                cycles_ += 12 + calc_ea_t_bw(dir);
                uint8_t value = read_b(dir);
                cc.z = ((value >> (bit & 7)) & 1) == 0;
                write_b2(dir, uint8_t(value ^ (1u << (bit & 7))));
            }
            break;
        }
        case 0x22: {  // bclr static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 12;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
                d[orig].l &= ~(1u << (bit & 0x1f));
            } else {
                cycles_ += 12 + calc_ea_t_bw(dir);
                uint8_t value = read_b(dir);
                cc.z = ((value >> (bit & 7)) & 1) == 0;
                write_b2(dir, uint8_t(value & ~(1u << (bit & 7))));
            }
            break;
        }
        case 0x23: {  // bset static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 12;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
                d[orig].l |= 1u << (bit & 0x1f);
            } else {
                cycles_ += 12 + calc_ea_t_bw(dir);
                uint8_t value = read_b(dir);
                cc.z = ((value >> (bit & 7)) & 1) == 0;
                write_b2(dir, uint8_t(value | (1u << (bit & 7))));
            }
            break;
        }
        case 0x28: {  // eori.b
            const uint8_t immediate = uint8_t(fetch_word());
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint8_t result = uint8_t(read_b(dir) ^ immediate);
                write_b2(dir, result);
                cc.v = false;
                cc.c = false;
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
            } else {  // eori to ccr
                cycles_ += 20;
                const uint8_t result = uint8_t((get_flags() & 0xff) ^ immediate);
                cc.x = (result & 0x10) != 0;
                cc.n = (result & 0x08) != 0;
                cc.z = (result & 0x04) != 0;
                cc.v = (result & 0x02) != 0;
                cc.c = (result & 0x01) != 0;
            }
            break;
        }
        case 0x29: {  // eori.w
            const uint16_t immediate = fetch_word();
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint16_t result = uint16_t(read_w(dir) ^ immediate);
                write_w2(dir, result);
                cc.v = false;
                cc.c = false;
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
            } else if (check_supervisor()) {  // eori to sr
                cycles_ += 20;
                set_flags(uint16_t(get_flags() ^ immediate));
            }
            break;
        }
        case 0x2a: {  // eori.l
            if ((dir >> 3) != 0) cycles_ += 20 + calc_ea_t_l(dir);
            else cycles_ += type_ == Type::M68010 ? 14 : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t result = read_l(dir) ^ immediate;
            write_l2(dir, result);
            cc.v = false;
            cc.c = false;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x30: {  // cmpi.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 8;
            const uint8_t immediate = uint8_t(fetch_word() & 0xff);
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(value - immediate);
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.v = (((immediate ^ value) & (result ^ value)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            break;
        }
        case 0x31: {  // cmpi.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 8;
            const uint16_t immediate = fetch_word();
            const uint16_t value = read_w(dir);
            const uint32_t result = uint32_t(value) - immediate;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            break;
        }
        case 0x32: {  // cmpi.l
            if (type_ == Type::M68010 || (dir >> 3) != 0) cycles_ += 12 + calc_ea_t_l(dir);
            else cycles_ += 14;
            const uint32_t immediate = fetch_long();
            const uint32_t value = read_l(dir);
            const uint32_t result = value - immediate;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
            cc.c = ((((immediate & result) | (~value & (immediate | result))) >> 23) & 0x100) != 0;
            break;
        }
        default:
            // Illegal instruction, vector 4.
            pc_.l = ppc_.l;
            exception(0x10, 34);
            break;
    }
}

void M68000::group_b(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // cmp.b
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t right = read_b(dir);
            const uint8_t left = d[dest].l0();
            const uint16_t result = uint16_t(left - right);
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            break;
        }
        case 0x1: {  // cmp.w
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t right = read_w(dir);
            const uint16_t left = d[dest].wl();
            const uint32_t result = uint32_t(left) - right;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            break;
        }
        case 0x2: {  // cmp.l
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = d[dest].l;
            const uint32_t result = left - right;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            break;
        }
        case 0x3: {  // cmpa.w
            cycles_ += 6 + calc_ea_t_bw(dir);
            const uint32_t right = uint32_t(int32_t(int16_t(read_w(dir))));
            const uint32_t left = a[dest].l;
            const uint32_t result = left - right;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 1) {  // cmpm.b
                cycles_ += 12;
                const uint8_t right = getbyte(a[orig].l);
                a[orig].l += 1;
                const uint8_t left = getbyte(a[dest].l);
                a[dest].l += 1;
                const uint16_t result = uint16_t(left - right);
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
            } else {  // eor.b
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
                const uint8_t result = uint8_t(read_b(dir) ^ d[dest].l0());
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 1) {  // cmpm.w
                cycles_ += 12;
                const uint16_t right = getword(a[orig].l);
                a[orig].l += 2;
                const uint16_t left = getword(a[dest].l);
                a[dest].l += 2;
                const uint32_t result = uint32_t(left) - right;
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.c = (result & 0x10000) != 0;
            } else {  // eor.w
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
                const uint16_t result = uint16_t(read_w(dir) ^ d[dest].wl());
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 1) {  // cmpm.l
                cycles_ += 20;
                const uint32_t right = (uint32_t(getword(a[orig].l)) << 16) | getword(a[orig].l + 2);
                a[orig].l += 4;
                const uint32_t left = (uint32_t(getword(a[dest].l)) << 16) | getword(a[dest].l + 2);
                a[dest].l += 4;
                const uint32_t result = left - right;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            } else {  // eor.l
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 8;
                const uint32_t result = read_l(dir) ^ d[dest].l;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            }
            break;
        }
        default: {  // cmpa.l
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = a[dest].l;
            const uint32_t result = left - right;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            break;
        }
    }
}

void M68000::group_c(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // and.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t result = uint8_t(d[dest].l0() & read_b(dir));
            d[dest].set_l0(result);
            cc.n = (result & 0x80) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x1: {  // and.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t result = uint16_t(d[dest].wl() & read_w(dir));
            d[dest].set_wl(result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x2: {  // and.l ea,Dn
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t result = d[dest].l & read_l(dir);
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x3: {  // mulu
            cycles_ += type_ == Type::M68010 ? 30 : 54;
            if ((dir >> 3) == 0) cycles_ += calc_ea_t_bw(dir);
            const uint32_t result = uint32_t(read_w(dir)) * d[dest].wl();
            d[dest].l = result;
            cc.z = result == 0;
            cc.n = (result & 0x80000000u) != 0;
            cc.v = false;
            cc.c = false;
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // abcd
                uint8_t right = 0;
                uint8_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 6;
                    right = d[orig].l0();
                    left = d[dest].l0();
                } else {
                    cycles_ += 18;
                    a[orig].l -= 1;
                    right = getbyte(a[orig].l);
                    a[dest].l -= 1;
                    left = getbyte(a[dest].l);
                }
                uint32_t result = uint32_t((right & 0x0f) + (left & 0x0f) + (cc.x ? 1 : 0));
                const uint32_t correction = result > 9 ? 6u : 0u;
                result += uint32_t((right & 0xf0) + (left & 0xf0));
                cc.v = (~result & 0x80) != 0;
                result += correction;
                cc.c = result > 0x9f;
                cc.x = cc.c;
                if (cc.c) result -= 0xa0;
                cc.n = (result & 0x80) != 0;
                cc.v = cc.v && cc.n;
                cc.z = (result & 0xff) == 0;
                if ((dir >> 3) == 0) d[dest].set_l0(uint8_t(result));
                else putbyte(a[dest].l, uint8_t(result));
            } else {  // and.b Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t result = uint8_t(read_b(dir) & d[dest].l0());
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 0) {  // exg Dx,Dy
                cycles_ += 6;
                std::swap(d[dest].l, d[orig].l);
            } else if ((dir >> 3) == 1) {  // exg Ax,Ay
                cycles_ += 6;
                std::swap(a[dest].l, a[orig].l);
            } else {  // and.w Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint16_t result = uint16_t(read_w(dir) & d[dest].wl());
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 1) {  // exg Dx,Ay
                cycles_ += 6;
                std::swap(d[dest].l, a[orig].l);
            } else {  // and.l Dn,ea
                cycles_ += 12 + calc_ea_t_l(dir);
                const uint32_t result = read_l(dir) & d[dest].l;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        default: {  // muls
            cycles_ += type_ == Type::M68010 ? 32 : 54;
            if ((dir >> 3) == 0) cycles_ += calc_ea_t_bw(dir);
            const int32_t right = int32_t(int16_t(read_w(dir)));
            const int32_t left = int32_t(int16_t(d[dest].wl()));
            const uint32_t result = uint32_t(right * left);
            d[dest].l = result;
            cc.z = result == 0;
            cc.n = (result & 0x80000000u) != 0;
            cc.v = false;
            cc.c = false;
            break;
        }
    }
}

void M68000::group_d(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // add.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t right = read_b(dir);
            const uint8_t left = d[dest].l0();
            const uint16_t result = uint16_t(right + left);
            d[dest].set_l0(uint8_t(result));
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x1: {  // add.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t right = read_w(dir);
            const uint16_t left = d[dest].wl();
            const uint32_t result = uint32_t(right) + left;
            d[dest].set_wl(uint16_t(result));
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.v = ((((right ^ result) & (left ^ result)) >> 8) & 0x80) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x2: {  // add.l ea,Dn
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = d[dest].l;
            const uint32_t result = right + left;
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
            cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x3: {  // adda.w
            cycles_ += 8 + calc_ea_t_bw(dir);
            a[dest].l += uint32_t(int32_t(int16_t(read_w(dir))));
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.b
                uint8_t right = 0;
                uint8_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 4;
                    right = d[orig].l0();
                    left = d[dest].l0();
                } else {
                    cycles_ += 18;
                    a[orig].l -= 1;
                    right = getbyte(a[orig].l);
                    a[dest].l -= 1;
                    left = getbyte(a[dest].l);
                }
                const uint16_t result = uint16_t(right + left + (cc.x ? 1 : 0));
                if ((dir >> 3) == 0) d[dest].set_l0(uint8_t(result));
                else putbyte(a[dest].l, uint8_t(result));
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
            } else {  // add.b Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t right = d[dest].l0();
                const uint8_t left = read_b(dir);
                const uint16_t result = uint16_t(right + left);
                write_b2(dir, uint8_t(result));
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.w
                uint16_t right = 0;
                uint16_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 4;
                    right = d[orig].wl();
                    left = d[dest].wl();
                } else {
                    cycles_ += 18;
                    a[orig].l -= 2;
                    right = getword(a[orig].l);
                    a[dest].l -= 2;
                    left = getword(a[dest].l);
                }
                const uint32_t result = uint32_t(right) + left + (cc.x ? 1 : 0);
                if ((dir >> 3) == 0) d[dest].set_wl(uint16_t(result));
                else putword(a[dest].l, uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
            } else {  // add.w Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint16_t right = d[dest].wl();
                const uint16_t left = read_w(dir);
                const uint32_t result = uint32_t(right) + left;
                write_w2(dir, uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ result) & (left ^ result)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.l
                uint32_t right = 0;
                uint32_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 8;
                    right = d[orig].l;
                    left = d[dest].l;
                } else {
                    cycles_ += 30;
                    a[orig].l -= 4;
                    right = (uint32_t(getword(a[orig].l)) << 16) | getword(a[orig].l + 2);
                    a[dest].l -= 4;
                    left = (uint32_t(getword(a[dest].l)) << 16) | getword(a[dest].l + 2);
                }
                const uint32_t result = right + left + (cc.x ? 1 : 0);
                if ((dir >> 3) == 0) {
                    d[dest].l = result;
                } else {
                    putword(a[dest].l, uint16_t(result >> 16));
                    putword(a[dest].l + 2, uint16_t(result));
                }
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else {  // add.l Dn,ea
                cycles_ += 12 + calc_ea_t_l(dir);
                const uint32_t right = d[dest].l;
                const uint32_t left = read_l(dir);
                const uint32_t result = right + left;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            }
            break;
        }
        default: {  // adda.l
            cycles_ += 6 + calc_ea_t_l(dir);
            a[dest].l += read_l(dir);
            break;
        }
    }
}

void M68000::group_e(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t orig = size_t(instruction & 7);

    if (((instruction >> 6) & 3) == 3) {  // memory shifts, always one bit
        cycles_ += 8 + calc_ea_t_bw(dir);
        const uint16_t value = read_w(dir);
        uint16_t result = 0;
        switch ((instruction >> 8) & 0x0f) {
            case 0x0:  // asr.w
                cc.c = (value & 1) != 0;
                result = uint16_t((value >> 1) | (value & 0x8000));
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x1:  // asl.w
                result = uint16_t(value << 1);
                cc.c = (value & 0x8000) != 0;
                cc.x = cc.c;
                cc.v = (value & 0xc000) != 0 && (value & 0xc000) != 0xc000;
                break;
            case 0x2:  // lsr.w
                cc.c = (value & 1) != 0;
                result = uint16_t(value >> 1);
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x3:  // lsl.w
                cc.c = (value & 0x8000) != 0;
                result = uint16_t(value << 1);
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x4:  // roxr.w
                cc.c = (value & 1) != 0;
                result = uint16_t((value >> 1) | (cc.x ? 0x8000 : 0));
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x5:  // roxl.w
                cc.c = (value & 0x8000) != 0;
                result = uint16_t((value << 1) | (cc.x ? 1 : 0));
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x6:  // ror.w
                cc.c = (value & 1) != 0;
                result = uint16_t((value >> 1) | ((value & 1) << 15));
                cc.v = false;
                break;
            case 0x7:  // rol.w
                cc.c = (value & 0x8000) != 0;
                result = uint16_t((value << 1) | (cc.c ? 1 : 0));
                cc.v = false;
                break;
            default:
                pc_.l = ppc_.l;
                exception(0x10, 34);
                return;
        }
        write_w2(dir, result);
        cc.n = (result & 0x8000) != 0;
        cc.z = result == 0;
        return;
    }

    const int count = ((instruction >> 5) & 1) == 1
                          ? int(d[size_t((instruction >> 9) & 7)].l & 0x3f)
                          : int(((((instruction >> 9) & 7) - 1) & 7) + 1);
    cycles_ += count * 2;
    const uint8_t op = uint8_t((instruction >> 3) & 0x3f);
    const int size = (op & 0x18) == 0x00 ? 8 : ((op & 0x18) == 0x08 ? 16 : 32);
    const bool left_shift = (op & 0x20) != 0;
    cycles_ += size == 32 ? 8 : 6;

    uint32_t value = size == 8 ? d[orig].l0() : (size == 16 ? d[orig].wl() : d[orig].l);
    const uint32_t original = value;
    const uint32_t mask = size == 32 ? 0xffffffffu : (1u << size) - 1;
    const uint32_t sign = 1u << (size - 1);
    bool overflow = false;

    switch (op & 0x23) {
        case 0x00:  // asr
            for (int step = 0; step < count; ++step) {
                cc.c = (value & 1) != 0;
                cc.x = cc.c;
                value = ((value & sign) != 0) ? (sign | (value >> 1)) : (value >> 1);
            }
            break;
        case 0x01:  // lsr
            for (int step = 0; step < count; ++step) {
                cc.c = (value & 1) != 0;
                cc.x = cc.c;
                value >>= 1;
            }
            break;
        case 0x02:  // roxr
            for (int step = 0; step < count; ++step) {
                const bool carry = (value & 1) != 0;
                value = ((value >> 1) | (cc.x ? sign : 0)) & mask;
                cc.c = carry;
                cc.x = carry;
            }
            break;
        case 0x03:  // ror
            for (int step = 0; step < count; ++step) {
                cc.c = (value & 1) != 0;
                value = ((value >> 1) | (cc.c ? sign : 0)) & mask;
            }
            break;
        case 0x20:  // asl
            for (int step = 0; step < count; ++step) {
                cc.c = (value & sign) != 0;
                cc.x = cc.c;
                value = (value << 1) & mask;
            }
            if (count != 0) {
                const uint32_t mask_out = size == 8 ? kShift8[std::min(count + 1, 64)]
                                          : (size == 16 ? shift16(count + 1) : shift32(count + 1));
                const uint32_t bits = original & mask_out;
                overflow = bits != 0 && bits != mask_out;
            }
            break;
        case 0x21:  // lsl
            for (int step = 0; step < count; ++step) {
                cc.c = (value & sign) != 0;
                cc.x = cc.c;
                value = (value << 1) & mask;
            }
            break;
        case 0x22:  // roxl
            for (int step = 0; step < count; ++step) {
                const bool carry = (value & sign) != 0;
                value = ((value << 1) | (cc.x ? 1 : 0)) & mask;
                cc.c = carry;
                cc.x = carry;
            }
            break;
        default:  // rol
            for (int step = 0; step < count; ++step) {
                cc.c = (value & sign) != 0;
                value = ((value << 1) | (cc.c ? 1 : 0)) & mask;
            }
            break;
    }

    value &= mask;
    if (size == 8) d[orig].set_l0(uint8_t(value));
    else if (size == 16) d[orig].set_wl(uint16_t(value));
    else d[orig].l = value;
    cc.n = (value & sign) != 0;
    cc.z = value == 0;
    // lsr/asr always clear n on the logical variants, matching the source.
    if (!left_shift && (op & 0x03) == 0x01) cc.n = false;
    cc.v = overflow;
}

void M68000::group_8(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // or.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t result = uint8_t(d[dest].l0() | read_b(dir));
            d[dest].set_l0(result);
            cc.n = (result & 0x80) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x1: {  // or.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t result = uint16_t(d[dest].wl() | read_w(dir));
            d[dest].set_wl(result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x2: {  // or.l ea,Dn
            cycles_ += (dir >> 3) != 0 ? 6 + calc_ea_t_l(dir) : 8;
            const uint32_t result = d[dest].l | read_l(dir);
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x3: {  // divu
            cycles_ += (type_ == Type::M68010 ? 108 : 140) + calc_ea_t_bw(dir);
            const uint16_t divisor = read_w(dir);
            if (divisor == 0) {
                pc_.l = ppc_.l;
                exception(0x14, 38);  // divide by zero
                break;
            }
            cc.c = false;
            const uint32_t quotient = d[dest].l / divisor;
            if (quotient < 0x10000) {
                cc.z = quotient == 0;
                cc.n = (quotient & 0x8000) != 0;
                cc.v = false;
                const uint32_t remainder = d[dest].l % divisor;
                d[dest].l = (quotient & 0xffff) | ((remainder & 0xffff) << 16);
            } else {
                cc.v = true;
            }
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0) {  // sbcd Dy,Dx
                cycles_ += 6;
                const uint8_t left = d[dest].l0();
                const uint8_t right = d[orig].l0();
                uint32_t result = uint32_t((left & 0x0f) - (right & 0x0f) - (cc.x ? 1 : 0));
                const uint32_t correction = result > 0x0f ? 6u : 0u;
                result += uint32_t((left & 0xf0) - (right & 0xf0));
                cc.v = result != 0;
                if (result > 0xff) {
                    result += 0xa0;
                    cc.x = true;
                    cc.c = true;
                } else if (result < correction) {
                    cc.x = true;
                    cc.c = true;
                } else {
                    cc.x = false;
                    cc.c = false;
                    cc.n = false;
                }
                result = (result - correction) & 0xff;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                cc.v = cc.v && cc.n;
                d[dest].set_l0(uint8_t(result));
            } else if ((dir >> 3) == 1) {  // sbcd -(Ay),-(Ax)
                cycles_ += 18;
                a[orig].l -= 1;
                const uint8_t right = getbyte(a[orig].l);
                a[dest].l -= 1;
                const uint8_t left = getbyte(a[dest].l);
                uint32_t result = uint32_t((left & 0x0f) - (right & 0x0f) - (cc.x ? 1 : 0));
                const uint32_t correction = result > 0x0f ? 6u : 0u;
                result += uint32_t((left & 0xf0) - (right & 0xf0));
                cc.v = result != 0;
                if (result > 0xff) {
                    result += 0xa0;
                    cc.x = true;
                    cc.c = true;
                } else if (result < correction) {
                    cc.x = true;
                    cc.c = true;
                } else {
                    cc.x = false;
                    cc.c = false;
                }
                result = (result - correction) & 0xff;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                cc.v = cc.v && cc.n;
                putbyte(a[dest].l, uint8_t(result));
            } else {  // or.b Dn,ea
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 12;
                const uint8_t result = uint8_t(read_b(dir) | d[dest].l0());
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        case 0x5: {  // or.w Dn,ea
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 12;
            const uint16_t result = uint16_t(read_w(dir) | d[dest].wl());
            write_w2(dir, result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x6: {  // or.l Dn,ea
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 20;
            const uint32_t result = read_l(dir) | d[dest].l;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        default: {  // divs
            cycles_ += (type_ == Type::M68010 ? 122 : 158) + calc_ea_t_bw(dir);
            const int32_t divisor = int32_t(int16_t(read_w(dir)));
            if (divisor == 0) {
                pc_.l = ppc_.l;
                exception(0x14, 38);  // divide by zero
                break;
            }
            const uint32_t value = d[dest].l;
            cc.c = false;
            if (value == 0x80000000u && divisor == -1) {
                cc.z = true;
                cc.n = false;
                cc.v = false;
                d[dest].l = 0;
            } else {
                const int32_t quotient = int32_t(value) / divisor;
                const int32_t remainder = int32_t(value) % divisor;
                if (quotient == int32_t(int16_t(quotient))) {
                    cc.z = quotient == 0;
                    cc.n = (quotient & 0x8000) != 0;
                    cc.v = false;
                    d[dest].l = (uint32_t(quotient) & 0xffff) | (uint32_t(remainder) << 16);
                } else {
                    cc.v = true;
                }
            }
            break;
        }
    }
}

void M68000::group_9(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // sub.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t right = read_b(dir);
            const uint8_t left = d[dest].l0();
            const uint16_t result = uint16_t(left - right);
            d[dest].set_l0(uint8_t(result));
            cc.n = (result & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            break;
        }
        case 0x1: {  // sub.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t right = read_w(dir);
            const uint16_t left = d[dest].wl();
            const uint32_t result = uint32_t(left) - right;
            d[dest].set_wl(uint16_t(result));
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x2: {  // sub.l ea,Dn
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = d[dest].l;
            const uint32_t result = left - right;
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x3: {  // suba.w
            cycles_ += 8 + calc_ea_t_bw(dir);
            a[dest].l -= uint32_t(int32_t(int16_t(read_w(dir))));
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0) {  // subx.b Dy,Dx
                cycles_ += 4;
                const uint8_t right = d[orig].l0();
                const uint8_t left = d[dest].l0();
                const uint16_t result = uint16_t(left - right - (cc.x ? 1 : 0));
                d[dest].set_l0(uint8_t(result));
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
            } else if ((dir >> 3) == 1) {  // subx.b -(Ay),-(Ax)
                cycles_ += 18;
                a[orig].l -= 1;
                const uint8_t right = getbyte(a[orig].l);
                a[dest].l -= 1;
                const uint8_t left = getbyte(a[dest].l);
                const uint16_t result = uint16_t(left - right - (cc.x ? 1 : 0));
                putbyte(a[dest].l, uint8_t(result));
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
            } else {  // sub.b Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t right = d[dest].l0();
                const uint8_t left = read_b(dir);
                const uint16_t result = uint16_t(left - right);
                write_b2(dir, uint8_t(result));
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 0) {  // subx.w Dy,Dx
                cycles_ += 4;
                const uint16_t right = d[orig].wl();
                const uint16_t left = d[dest].wl();
                const uint32_t result = uint32_t(left) - right - (cc.x ? 1 : 0);
                d[dest].set_wl(uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            } else if ((dir >> 3) == 1) {  // subx.w -(Ay),-(Ax)
                cycles_ += 18;
                a[orig].l -= 2;
                const uint16_t right = getword(a[orig].l);
                a[dest].l -= 2;
                const uint16_t left = getword(a[dest].l);
                const uint32_t result = uint32_t(left) - right - (cc.x ? 1 : 0);
                putword(a[dest].l, uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            } else {  // sub.w Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint16_t right = d[dest].wl();
                const uint16_t left = read_w(dir);
                const uint32_t result = uint32_t(left) - right;
                write_w2(dir, uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 0) {  // subx.l Dy,Dx
                cycles_ += 8;
                const uint32_t right = d[orig].l;
                const uint32_t left = d[dest].l;
                const uint32_t result = left - right - (cc.x ? 1 : 0);
                d[dest].l = result;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else if ((dir >> 3) == 1) {  // subx.l -(Ay),-(Ax)
                cycles_ += 30;
                a[orig].l -= 4;
                const uint32_t right = (uint32_t(getword(a[orig].l)) << 16) | getword(a[orig].l + 2);
                a[dest].l -= 4;
                const uint32_t left = (uint32_t(getword(a[dest].l)) << 16) | getword(a[dest].l + 2);
                const uint32_t result = left - right - (cc.x ? 1 : 0);
                putword(a[dest].l, uint16_t(result >> 16));
                putword(a[dest].l + 2, uint16_t(result));
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else {  // sub.l Dn,ea
                cycles_ += 12 + calc_ea_t_l(dir);
                const uint32_t right = d[dest].l;
                const uint32_t left = read_l(dir);
                const uint32_t result = left - right;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            }
            break;
        }
        default: {  // suba.l
            cycles_ += 6 + calc_ea_t_l(dir);
            a[dest].l -= read_l(dir);
            break;
        }
    }
}

void M68000::group_5(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t orig = size_t(instruction & 7);
    const uint8_t quick = uint8_t(((((instruction >> 9) & 7) - 1) & 7) + 1);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // addq.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(quick + value);
            write_b2(dir, uint8_t(result));
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.v = (((quick ^ result) & (value ^ result)) & 0x80) != 0;
            break;
        }
        case 0x1: {  // addq.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            if ((dir >> 3) != 1) {
                const uint16_t value = read_w(dir);
                const uint32_t result = uint32_t(value) + quick;
                write_w2(dir, uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((quick ^ result) & (value ^ result)) >> 8) & 0x80) != 0;
            } else {
                a[orig].l += quick;
            }
            break;
        }
        case 0x2: {  // addq.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 8;
            if ((dir >> 3) != 1) {
                const uint32_t value = read_l(dir);
                const uint32_t result = value + quick;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.c = ((((quick & value) | (~result & (quick | value))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                cc.v = ((((quick ^ result) & (value ^ result)) >> 24) & 0x80) != 0;
            } else {
                a[orig].l += quick;
            }
            break;
        }
        case 0x3: case 0x7: {
            if (((dir >> 3) & 7) == 1) {  // dbcc
                cycles_ += 12;
                if (!condition(uint8_t((instruction >> 8) & 0x0f))) {
                    d[orig].set_wl(uint16_t(d[orig].wl() - 1));
                    if (d[orig].wl() != 0xffff) {
                        cycles_ -= 2;
                        pc_.l += uint32_t(int32_t(int16_t(getword(pc_.l))));
                    } else {
                        pc_.l += 2;
                    }
                } else {
                    pc_.l += 2;
                }
            } else {  // scc
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
                write_b(dir, condition(uint8_t((instruction >> 8) & 0x0f)) ? 0xff : 0x00);
            }
            break;
        }
        case 0x4: {  // subq.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(value - quick);
            write_b2(dir, uint8_t(result));
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.v = (((quick ^ value) & (result ^ value)) & 0x80) != 0;
            break;
        }
        case 0x5: {  // subq.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            if ((dir >> 3) != 1) {
                const uint16_t value = read_w(dir);
                const uint32_t result = uint32_t(value) - quick;
                write_w2(dir, uint16_t(result));
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((quick ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
            } else {
                a[orig].l -= quick;
            }
            break;
        }
        default: {  // subq.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 8;
            if ((dir >> 3) != 1) {
                const uint32_t value = read_l(dir);
                const uint32_t result = value - quick;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((quick ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
                cc.c = ((((quick & result) | (~value & (quick | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else {
                a[orig].l -= quick;
            }
            break;
        }
    }
}

void M68000::group_6(uint16_t instruction) {
    const uint8_t offset = uint8_t(instruction & 0xff);
    const uint8_t code = uint8_t((instruction >> 8) & 0x0f);

    if (code == 1) {  // bsr
        cycles_ += 18;
        if (offset == 0x00) {
            const uint16_t displacement = getword(pc_.l);
            a[7].l -= 4;
            putword(a[7].l, uint16_t((pc_.l + 2) >> 16));
            putword(a[7].l + 2, uint16_t(pc_.l + 2));
            pc_.l += uint32_t(int32_t(int16_t(displacement)));
        } else {
            a[7].l -= 4;
            putword(a[7].l, uint16_t(pc_.l >> 16));
            putword(a[7].l + 2, uint16_t(pc_.l));
            pc_.l += uint32_t(int32_t(int8_t(offset)));
        }
        return;
    }

    // bra is the same as bcc with the always true condition.
    if (condition(code)) {
        cycles_ += 10;
        if (offset == 0x00) pc_.l += uint32_t(int32_t(int16_t(getword(pc_.l))));
        else pc_.l += uint32_t(int32_t(int8_t(offset)));
    } else {
        cycles_ += 8;
        if (offset == 0x00) pc_.l += 2;
    }
}

void M68000::group_7(uint16_t instruction) {  // moveq
    cycles_ += 4;
    const size_t dest = size_t((instruction >> 9) & 7);
    d[dest].l = uint32_t(int32_t(int8_t(instruction & 0xff)));
    cc.c = false;
    cc.v = false;
    cc.z = d[dest].l == 0;
    cc.n = (d[dest].l & 0x80000000u) != 0;
}

void M68000::move_bw(uint16_t instruction, bool byte_size) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const uint8_t target = uint8_t(((instruction >> 9) & 7) | (((instruction >> 6) & 7) << 3));
    if (byte_size) {
        const uint8_t value = read_b(dir);
        write_b(target, value);
        cycles_ += calc_move_t(dir, target, false);
        cc.n = (value & 0x80) != 0;
        cc.z = value == 0;
    } else {
        const uint16_t value = read_w(dir);
        write_w(target, value);
        cycles_ += calc_move_t(dir, target, false);
        cc.n = (value & 0x8000) != 0;
        cc.z = value == 0;
    }
    cc.v = false;
    cc.c = false;
}

void M68000::move_l(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const uint8_t target = uint8_t(((instruction >> 9) & 7) | (((instruction >> 6) & 7) << 3));
    const uint32_t value = read_l(dir);
    write_l(target, value);
    cycles_ += calc_move_t(dir, target, true);
    cc.v = false;
    cc.c = false;
    cc.n = (value & 0x80000000u) != 0;
    cc.z = value == 0;
}

void M68000::group_1(uint16_t instruction) { move_bw(instruction, true); }

void M68000::group_4(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    const size_t dest = size_t((instruction >> 9) & 7);
    const size_t orig = size_t(instruction & 7);
    const uint8_t op = uint8_t((instruction >> 6) & 0x3f);

    switch (op) {
        case 0x00: {  // negx.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(0 - value - (cc.x ? 1 : 0));
            cc.n = (result & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            if ((result & 0xff) != 0) cc.z = false;
            cc.v = ((value & result) & 0x80) != 0;
            write_b2(dir, uint8_t(result));
            break;
        }
        case 0x01: {  // negx.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint16_t value = read_w(dir);
            const uint32_t result = uint32_t(0 - value - (cc.x ? 1 : 0));
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            if ((result & 0xffff) != 0) cc.z = false;
            cc.v = (((value & result) >> 8) & 0x80) != 0;
            write_w2(dir, uint16_t(result));
            break;
        }
        case 0x02: {  // negx.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            const uint32_t value = read_l(dir);
            const uint32_t result = 0 - value - (cc.x ? 1 : 0);
            cc.n = (result & 0x80000000u) != 0;
            cc.c = ((((value & result) | (value | result)) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            cc.z = result == 0;
            cc.v = (((value & result) >> 24) & 0x80) != 0;
            write_l2(dir, result);
            break;
        }
        case 0x03: {  // move from sr
            if (type_ == Type::M68000) {
                cycles_ += (dir >> 3) == 0 ? 6 : 8 + calc_ea_t_bw(dir);
                write_w(dir, get_flags());
            } else if (check_supervisor()) {
                cycles_ += (dir >> 3) == 0 ? 4 : 8 + calc_ea_t_bw(dir);
                write_w(dir, get_flags());
            }
            break;
        }
        case 0x06: case 0x0e: case 0x16: case 0x1e:
        case 0x26: case 0x2e: case 0x36: case 0x3e: {  // chk
            cycles_ += 10 + calc_ea_t_bw(dir);
            const int16_t bound = int16_t(read_w(dir));
            const int16_t value = int16_t(d[dest].wl());
            cc.n = value < 0;
            if (value < 0 || value > bound) {
                pc_.l = ppc_.l;
                exception(0x18, 30);
            }
            break;
        }
        case 0x07: case 0x0f: case 0x17: case 0x1f:
        case 0x27: case 0x2f: case 0x37: case 0x3f: {  // lea
            a[dest].l = read_ea(dir);
            if (dir <= 0x17) cycles_ += 4;
            else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 8;
            else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b || dir == 0x39) cycles_ += 12;
            break;
        }
        case 0x08: {  // clr.b
            if (type_ == Type::M68000) cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            else cycles_ += 4 + calc_ea_t_bw(dir);
            write_b(dir, 0);
            cc.n = false;
            cc.v = false;
            cc.c = false;
            cc.z = true;
            break;
        }
        case 0x09: {  // clr.w
            if (type_ == Type::M68000) cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            else cycles_ += 4 + calc_ea_t_bw(dir);
            write_w(dir, 0);
            cc.n = false;
            cc.v = false;
            cc.c = false;
            cc.z = true;
            break;
        }
        case 0x0a: {  // clr.l
            if (type_ == Type::M68000) cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            else cycles_ += (dir >> 3) != 0 ? 4 + calc_ea_t_l(dir) : 6;
            write_l(dir, 0);
            cc.n = false;
            cc.v = false;
            cc.c = false;
            cc.z = true;
            break;
        }
        case 0x10: {  // neg.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = uint16_t(0 - value);
            cc.n = (result & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.z = (result & 0xff) == 0;
            cc.v = ((value & result) & 0x80) != 0;
            write_b2(dir, uint8_t(result));
            break;
        }
        case 0x11: {  // neg.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint16_t value = read_w(dir);
            const uint32_t result = uint32_t(0 - value);
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.z = (result & 0xffff) == 0;
            cc.v = (((value & result) >> 8) & 0x80) != 0;
            write_w2(dir, uint16_t(result));
            break;
        }
        case 0x12: {  // neg.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            const uint32_t value = read_l(dir);
            const uint32_t result = 0 - value;
            cc.n = (result & 0x80000000u) != 0;
            cc.c = ((((value & result) | (value | result)) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            cc.z = result == 0;
            cc.v = (((value & result) >> 24) & 0x80) != 0;
            write_l2(dir, result);
            break;
        }
        case 0x13: {  // move to ccr
            cycles_ += 12 + calc_ea_t_bw(dir);
            const uint16_t value = read_w(dir);
            cc.x = (value & 0x10) != 0;
            cc.n = (value & 0x08) != 0;
            cc.z = (value & 0x04) != 0;
            cc.v = (value & 0x02) != 0;
            cc.c = (value & 0x01) != 0;
            break;
        }
        case 0x18: {  // not.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t result = uint8_t(~read_b(dir));
            write_b2(dir, result);
            cc.c = false;
            cc.v = false;
            cc.n = (result & 0x80) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x19: {  // not.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint16_t result = uint16_t(~read_w(dir));
            write_w2(dir, result);
            cc.c = false;
            cc.v = false;
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x1a: {  // not.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            const uint32_t result = ~read_l(dir);
            write_l2(dir, result);
            cc.c = false;
            cc.v = false;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x1b: {  // move to sr
            if (check_supervisor()) {
                cycles_ += 12 + calc_ea_t_bw(dir);
                set_flags(read_w(dir));
            }
            break;
        }
        case 0x21: {  // swap / pea
            if (dir <= 0x07) {  // swap
                cycles_ += 4;
                const uint32_t result = (uint32_t(d[orig].wl()) << 16) | d[orig].wh();
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                d[orig].l = result;
            } else {  // pea
                const uint32_t address = read_ea(dir);
                if (dir <= 0x17) cycles_ += 12;
                else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 16;
                else if (dir >= 0x30 && dir <= 0x37) cycles_ += 20;
                else if (dir == 0x39) cycles_ += 20;
                a[7].l -= 4;
                putword(a[7].l, uint16_t(address >> 16));
                putword(a[7].l + 2, uint16_t(address));
            }
            break;
        }
        case 0x22: {  // ext.w / movem.w register to memory
            if ((dir >> 3) == 0) {
                cycles_ += 4;
                const uint16_t result = uint16_t(int16_t(int8_t(d[orig].l0())));
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                d[orig].set_wl(result);
            } else {
                const uint16_t mask = fetch_word();
                int count = 0;
                for (int bit = 0; bit < 16; ++bit) {
                    if (mask & (1u << bit)) ++count;
                }
                cycles_ += count << 2;
                if (dir <= 0x27) cycles_ += 12;
                else if (dir <= 0x2f || dir == 0x38) cycles_ += 16;
                else if (dir <= 0x37) cycles_ += 18;
                else if (dir == 0x39) cycles_ += 20;
                if (dir >= 0x20 && dir <= 0x27) {  // -(An), reversed order
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        const int index = 15 - bit;
                        write_w(dir, index < 8 ? d[size_t(index)].wl() : a[size_t(index - 8)].wl());
                    }
                } else {
                    uint32_t address = read_ea(dir);
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        putword(address, bit < 8 ? d[size_t(bit)].wl() : a[size_t(bit - 8)].wl());
                        address += 2;
                    }
                }
            }
            break;
        }
        case 0x23: {  // ext.l / movem.l register to memory
            if ((dir >> 3) == 0) {
                cycles_ += 4;
                const uint32_t result = uint32_t(int32_t(int16_t(d[orig].wl())));
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                d[orig].l = result;
            } else {
                const uint16_t mask = fetch_word();
                int count = 0;
                for (int bit = 0; bit < 16; ++bit) {
                    if (mask & (1u << bit)) ++count;
                }
                cycles_ += count << 3;
                if (dir <= 0x27) cycles_ += 8;
                else if (dir <= 0x2f || dir == 0x38) cycles_ += 12;
                else if (dir <= 0x37) cycles_ += 14;
                else if (dir == 0x39) cycles_ += 16;
                if (dir >= 0x20 && dir <= 0x27) {
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        const int index = 15 - bit;
                        write_l(dir, index < 8 ? d[size_t(index)].l : a[size_t(index - 8)].l);
                    }
                } else {
                    uint32_t address = read_ea(dir);
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        const uint32_t value = bit < 8 ? d[size_t(bit)].l : a[size_t(bit - 8)].l;
                        putword(address, uint16_t(value >> 16));
                        putword(address + 2, uint16_t(value));
                        address += 4;
                    }
                }
            }
            break;
        }
        case 0x28: {  // tst.b
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t value = read_b(dir);
            cc.v = false;
            cc.c = false;
            cc.n = (value & 0x80) != 0;
            cc.z = value == 0;
            break;
        }
        case 0x29: {  // tst.w
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t value = read_w(dir);
            cc.v = false;
            cc.c = false;
            cc.n = (value & 0x8000) != 0;
            cc.z = value == 0;
            break;
        }
        case 0x2a: {  // tst.l
            cycles_ += 4 + calc_ea_t_l(dir);
            const uint32_t value = read_l(dir);
            cc.v = false;
            cc.c = false;
            cc.n = (value & 0x80000000u) != 0;
            cc.z = value == 0;
            break;
        }
        case 0x2b: {  // tas
            cycles_ += (dir >> 3) != 0 ? 14 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            cc.z = value == 0;
            cc.n = (value & 0x80) != 0;
            cc.v = false;
            cc.c = false;
            write_b2(dir, uint8_t(value | 0x80));
            break;
        }
        case 0x32: {  // movem.w memory to register
            const uint16_t mask = fetch_word();
            int count = 0;
            for (int bit = 0; bit < 16; ++bit) {
                if (mask & (1u << bit)) ++count;
            }
            cycles_ += count << 2;
            if (dir <= 0x1f) cycles_ += 12;
            else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) cycles_ += 16;
            else if (dir <= 0x37 || dir == 0x3b) cycles_ += 18;
            else if (dir == 0x39) cycles_ += 20;
            if (dir >= 0x18 && dir <= 0x1f) {  // (An)+
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = uint32_t(int32_t(int16_t(read_w(dir))));
                    if (bit < 8) d[size_t(bit)].l = value;
                    else a[size_t(bit - 8)].l = value;
                }
            } else {
                uint32_t address = read_ea(dir);
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = uint32_t(int32_t(int16_t(getword(address))));
                    if (bit < 8) d[size_t(bit)].l = value;
                    else a[size_t(bit - 8)].l = value;
                    address += 2;
                }
            }
            break;
        }
        case 0x33: {  // movem.l memory to register
            const uint16_t mask = fetch_word();
            int count = 0;
            for (int bit = 0; bit < 16; ++bit) {
                if (mask & (1u << bit)) ++count;
            }
            cycles_ += count << 3;
            if (dir <= 0x1f) cycles_ += 12;
            else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) cycles_ += 16;
            else if (dir <= 0x37 || dir == 0x3b) cycles_ += 18;
            else if (dir == 0x39) cycles_ += 20;
            if (dir >= 0x18 && dir <= 0x1f) {  // (An)+
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = read_l(dir);
                    if (bit < 8) d[size_t(bit)].l = value;
                    else a[size_t(bit - 8)].l = value;
                }
            } else {
                uint32_t address = read_ea(dir);
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = (uint32_t(getword(address)) << 16) | getword(address + 2);
                    if (bit < 8) d[size_t(bit)].l = value;
                    else a[size_t(bit - 8)].l = value;
                    address += 4;
                }
            }
            break;
        }
        case 0x39: {  // trap, link, unlk, usp, reset, nop, stop, rte, rts, rtr
            if (dir <= 0x0f) {  // trap
                cycles_ += 38;
                const uint16_t flags = get_flags();
                set_flags(uint16_t(flags | 0x2000));
                a[7].l -= 6;
                putword(a[7].l + 4, pc_.wl());
                putword(a[7].l + 2, pc_.wh());
                putword(a[7].l, flags);
                opcode_ = false;
                pc_.set_wh(getword(0x80 + ((instruction & 0x0f) * 4)));
                pc_.set_wl(getword(0x82 + ((instruction & 0x0f) * 4)));
                opcode_ = true;
            } else if (dir <= 0x17) {  // link
                cycles_ += 16;
                const int16_t displacement = int16_t(fetch_word());
                a[7].l -= 4;
                putword(a[7].l, a[orig].wh());
                putword(a[7].l + 2, a[orig].wl());
                a[orig].l = a[7].l;
                a[7].l += uint32_t(int32_t(displacement));
            } else if (dir <= 0x1f) {  // unlk
                cycles_ += 12;
                a[7].l = a[orig].l;
                a[orig].set_wh(getword(a[7].l));
                a[orig].set_wl(getword(a[7].l + 2));
                a[7].l += 4;
            } else if (dir <= 0x2f) {  // move usp
                cycles_ += 4;
                if (check_supervisor()) {
                    if (((dir >> 3) & 1) == 1) a[orig].l = other_sp_.l;
                    else other_sp_.l = a[orig].l;
                }
            } else {
                switch (dir) {
                    case 0x30:  // reset
                        if (check_supervisor()) cycles_ += 40;
                        break;
                    case 0x31:  // nop
                        cycles_ += 4;
                        break;
                    case 0x32:  // stop
                        if (check_supervisor()) {
                            set_flags(fetch_word());
                            cycles_ += 4;
                            halted_ = true;
                        }
                        break;
                    case 0x33:  // rte
                        if (check_supervisor()) {
                            if (type_ == Type::M68000) {
                                cycles_ += 20;
                                const uint16_t flags = getword(a[7].l);
                                pc_.set_wh(getword(a[7].l + 2));
                                pc_.set_wl(getword(a[7].l + 4));
                                a[7].l += 6;
                                set_flags(flags);
                            } else {
                                cycles_ += 24;
                                const uint16_t flags = getword(a[7].l);
                                pc_.set_wh(getword(a[7].l + 2));
                                pc_.set_wl(getword(a[7].l + 4));
                                a[7].l += 8;  // the 68010 also stacks a format word
                                set_flags(flags);
                            }
                        }
                        break;
                    case 0x35:  // rts
                        cycles_ += 16;
                        pc_.set_wh(getword(a[7].l));
                        pc_.set_wl(getword(a[7].l + 2));
                        a[7].l += 4;
                        break;
                    case 0x37:  // rtr
                        cycles_ += 20;
                        set_flags(getword(a[7].l));
                        pc_.set_wh(getword(a[7].l + 2));
                        pc_.set_wl(getword(a[7].l + 4));
                        a[7].l += 6;
                        break;
                    default:
                        pc_.l = ppc_.l;
                        exception(0x10, 34);
                        break;
                }
            }
            break;
        }
        case 0x3a: {  // jsr
            const uint32_t address = read_ea(dir);
            a[7].l -= 4;
            putword(a[7].l, pc_.wh());
            putword(a[7].l + 2, pc_.wl());
            pc_.l = address;
            if (dir <= 0x17) cycles_ += 16;
            else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 18;
            else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b) cycles_ += 22;
            else if (dir == 0x39) cycles_ += 20;
            break;
        }
        case 0x3b: {  // jmp
            pc_.l = read_ea(dir);
            if (dir <= 0x17) cycles_ += 8;
            else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 10;
            else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b) cycles_ += 14;
            else if (dir == 0x39) cycles_ += 12;
            break;
        }
        default:
            pc_.l = ppc_.l;
            exception(0x10, 34);
            break;
    }
}

void M68000::group_2(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    if (((instruction >> 6) & 7) == 1) {  // movea.l
        cycles_ += (dir >> 3) > 1 ? 4 + calc_ea_t_l(dir) : 4;
        a[(instruction >> 9) & 7].l = read_l(dir);
    } else {
        move_l(instruction);
    }
}

void M68000::group_3(uint16_t instruction) {
    const uint8_t dir = uint8_t(instruction & 0x3f);
    if (((instruction >> 6) & 7) == 1) {  // movea.w
        cycles_ += (dir >> 3) > 1 ? 4 + calc_ea_t_bw(dir) : 4;
        a[(instruction >> 9) & 7].l = uint32_t(int32_t(int16_t(read_w(dir))));
    } else {
        move_bw(instruction, false);
    }
}

void M68000::group_f(uint16_t instruction) {  // line 1111 emulator
    (void)instruction;
    cycles_ += 4;
    const uint16_t flags = get_flags();
    set_flags(uint16_t(flags | 0x2000));
    a[7].l -= 6;
    putword(a[7].l, flags);
    putword(a[7].l + 2, pc_.wh());
    putword(a[7].l + 4, uint16_t(pc_.wl() - 2));
    pc_.set_wh(getword(0x0b * 4));
    pc_.set_wl(getword((0x0b * 4) + 2));
}

bool M68000::take_irq() {
    for (int level = 7; level >= 1; --level) {
        if (cc.im >= level || irq_[size_t(level)] == IrqLine::Clear) continue;
        halted_ = false;
        cycles_ += 44;
        const uint16_t flags = get_flags();
        set_flags(uint16_t(flags | 0x2000));
        if (type_ == Type::M68010) {
            a[7].l -= 2;
            putword(a[7].l, uint16_t(level << 2));
        }
        a[7].l -= 6;
        putword(a[7].l, flags);
        putword(a[7].l + 2, pc_.wh());
        putword(a[7].l + 4, pc_.wl());
        opcode_ = false;
        pc_.set_wh(getword(0x64 + uint32_t((level - 1) * 4)));
        pc_.set_wl(getword(0x66 + uint32_t((level - 1) * 4)));
        opcode_ = true;
        if (irq_[size_t(level)] == IrqLine::Hold) irq_[size_t(level)] = IrqLine::Clear;
        cc.im = uint8_t(level);
        return true;
    }
    return false;
}

int M68000::run(int cycles) {
    cycles_ = 0;
    while (cycles_ < cycles) {
        if (reset_request_ != IrqLine::Clear) {
            const IrqLine request = reset_request_;
            reset();
            if (request == IrqLine::Assert) reset_request_ = IrqLine::Assert;
            cycles_ = cycles;
            break;
        }
        if (halt_request_ != IrqLine::Clear) {
            cycles_ += 4;
            if (cycle_handler_) cycle_handler_(4);
            continue;
        }

        const int start = cycles_;
        take_irq();
        if (halted_) {  // stopped by the stop instruction until an interrupt arrives
            cycles_ += 4;
            if (cycle_handler_) cycle_handler_(4);
            continue;
        }

        ppc_ = pc_;
        const uint16_t instruction = fetch_word();
        switch (instruction >> 12) {
            case 0x0: group_0(instruction); break;
            case 0x1: group_1(instruction); break;
            case 0x2: group_2(instruction); break;
            case 0x3: group_3(instruction); break;
            case 0x4: group_4(instruction); break;
            case 0x5: group_5(instruction); break;
            case 0x6: group_6(instruction); break;
            case 0x7: group_7(instruction); break;
            case 0x8: group_8(instruction); break;
            case 0x9: group_9(instruction); break;
            case 0xb: group_b(instruction); break;
            case 0xc: group_c(instruction); break;
            case 0xd: group_d(instruction); break;
            case 0xe: group_e(instruction); break;
            case 0xf: group_f(instruction); break;
            default:
                pc_.l = ppc_.l;
                exception(0x10, 34);  // illegal instruction
                break;
        }
        if (cycle_handler_) cycle_handler_(cycles_ - start);
    }
    return cycles_;
}

}  // namespace dsp
