#include "cpu/w65c816.h"

namespace dsp {

W65C816::W65C816(uint32_t clock) : clock_(clock) {}

void W65C816::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void W65C816::reset() {
    e_ = true;
    p.m = true;
    p.x = true;
    p.i = true;
    p.d = false;
    a = x = y = 0;
    d = 0;
    dbr = 0;
    pbr = 0;
    sp = 0x01ff;
    pc_ = uint16_t(read(0xfffc) | (read(0xfffd) << 8));
    irq_state_ = IrqLine::Clear;
    irq_request_ = IrqLine::Clear;
    nmi_state_ = IrqLine::Clear;
    nmi_request_ = IrqLine::Clear;
    stopped_ = false;
    waiting_ = false;
}

void W65C816::set_nmi(IrqLine state) {
    nmi_request_ = state;
    if (state == IrqLine::Clear) nmi_state_ = IrqLine::Clear;
}

uint8_t W65C816::fetch8() {
    uint8_t v = read((uint32_t(pbr) << 16) | pc_);
    pc_ = uint16_t(pc_ + 1);
    return v;
}
uint16_t W65C816::fetch16() {
    uint8_t lo = fetch8();
    uint8_t hi = fetch8();
    return uint16_t(lo | (hi << 8));
}
uint32_t W65C816::fetch24() {
    uint32_t lo = fetch16();
    uint32_t hi = fetch8();
    return lo | (hi << 16);
}

uint8_t W65C816::get_p() const {
    uint8_t v = uint8_t(p.c) | (uint8_t(p.z) << 1) | (uint8_t(p.i) << 2) | (uint8_t(p.d) << 3) |
                (uint8_t(p.v) << 6) | (uint8_t(p.n) << 7);
    if (e_) {
        v |= 0x30;  // bit5 unused (always 1), bit4 "B" pushed set outside an interrupt
    } else {
        v |= uint8_t((uint8_t(p.m) << 5) | (uint8_t(p.x) << 4));
    }
    return v;
}
void W65C816::set_p(uint8_t value) {
    p.c = (value & 0x01) != 0;
    p.z = (value & 0x02) != 0;
    p.i = (value & 0x04) != 0;
    p.d = (value & 0x08) != 0;
    p.v = (value & 0x40) != 0;
    p.n = (value & 0x80) != 0;
    if (!e_) {
        p.x = (value & 0x10) != 0;
        p.m = (value & 0x20) != 0;
        if (p.x) {
            x &= 0xff;
            y &= 0xff;
        }
    } else {
        p.m = true;
        p.x = true;
    }
}

void W65C816::set_nz8(uint8_t v) {
    p.z = v == 0;
    p.n = (v & 0x80) != 0;
}
void W65C816::set_nz16(uint16_t v) {
    p.z = v == 0;
    p.n = (v & 0x8000) != 0;
}

void W65C816::push8(uint8_t value) {
    write((e_ ? 0x100 : 0) | sp, value);
    sp = uint16_t(sp - 1);
    if (e_) sp = uint16_t(0x100 | (sp & 0xff));
}
uint8_t W65C816::pop8() {
    sp = uint16_t(sp + 1);
    if (e_) sp = uint16_t(0x100 | (sp & 0xff));
    return read((e_ ? 0x100 : 0) | sp);
}
void W65C816::push16(uint16_t value) {
    push8(uint8_t(value >> 8));
    push8(uint8_t(value));
}
uint16_t W65C816::pop16() {
    uint8_t lo = pop8();
    uint8_t hi = pop8();
    return uint16_t(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// Addressing modes. Each fetches its own operand bytes from the instruction
// stream and returns the effective 24-bit (bank<<16 | offset) address.
// ---------------------------------------------------------------------------

uint32_t W65C816::addr_direct(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    return uint32_t(uint16_t(d + off));
}
uint32_t W65C816::addr_direct_x(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    return uint32_t(uint16_t(d + off + x));
}
uint32_t W65C816::addr_direct_y(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    return uint32_t(uint16_t(d + off + y));
}
uint32_t W65C816::addr_direct_indirect(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    uint16_t ptr_addr = uint16_t(d + off);
    uint16_t ptr = uint16_t(read(ptr_addr) | (read(uint16_t(ptr_addr + 1)) << 8));
    return (uint32_t(dbr) << 16) | ptr;
}
uint32_t W65C816::addr_direct_indirect_x(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    uint16_t ptr_addr = uint16_t(d + off + x);
    uint16_t ptr = uint16_t(read(ptr_addr) | (read(uint16_t(ptr_addr + 1)) << 8));
    return (uint32_t(dbr) << 16) | ptr;
}
uint32_t W65C816::addr_direct_indirect_y(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    uint16_t ptr_addr = uint16_t(d + off);
    uint16_t ptr = uint16_t(read(ptr_addr) | (read(uint16_t(ptr_addr + 1)) << 8));
    uint32_t base = (uint32_t(dbr) << 16) + ptr;
    if ((base & 0xff00) != ((base + y) & 0xff00)) extra += 1;
    return (base + y) & 0xffffff;
}
uint32_t W65C816::addr_direct_indirect_long(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    uint16_t ptr_addr = uint16_t(d + off);
    uint32_t ptr = uint32_t(read(ptr_addr)) | (uint32_t(read(uint16_t(ptr_addr + 1))) << 8) |
                   (uint32_t(read(uint16_t(ptr_addr + 2))) << 16);
    return ptr;
}
uint32_t W65C816::addr_direct_indirect_long_y(int& extra) {
    uint8_t off = fetch8();
    if (uint8_t(d) != 0) extra += 1;
    uint16_t ptr_addr = uint16_t(d + off);
    uint32_t ptr = uint32_t(read(ptr_addr)) | (uint32_t(read(uint16_t(ptr_addr + 1))) << 8) |
                   (uint32_t(read(uint16_t(ptr_addr + 2))) << 16);
    return (ptr + y) & 0xffffff;
}
uint32_t W65C816::addr_absolute() {
    uint16_t off = fetch16();
    return (uint32_t(dbr) << 16) | off;
}
uint32_t W65C816::addr_absolute_x(int& extra) {
    uint16_t off = fetch16();
    uint32_t base = (uint32_t(dbr) << 16) + off;
    if ((base & 0xff00) != ((base + x) & 0xff00)) extra += 1;
    return (base + x) & 0xffffff;
}
uint32_t W65C816::addr_absolute_y(int& extra) {
    uint16_t off = fetch16();
    uint32_t base = (uint32_t(dbr) << 16) + off;
    if ((base & 0xff00) != ((base + y) & 0xff00)) extra += 1;
    return (base + y) & 0xffffff;
}
uint32_t W65C816::addr_absolute_long() { return fetch24(); }
uint32_t W65C816::addr_absolute_long_x() { return (fetch24() + x) & 0xffffff; }
uint32_t W65C816::addr_stack_relative() {
    uint8_t off = fetch8();
    return uint32_t(uint16_t(sp + off));
}
uint32_t W65C816::addr_stack_relative_indirect_y() {
    uint8_t off = fetch8();
    uint16_t ptr_addr = uint16_t(sp + off);
    uint16_t ptr = uint16_t(read(ptr_addr) | (read(uint16_t(ptr_addr + 1)) << 8));
    return ((uint32_t(dbr) << 16) + ptr + y) & 0xffffff;
}

// ---------------------------------------------------------------------------
// Width-aware ALU / load / store / RMW helpers.
// ---------------------------------------------------------------------------

void W65C816::op_adc(uint32_t addr) {
    if (p.m) {
        uint8_t m = read(addr);
        if (p.d) {
            int lo = (a & 0xf) + (m & 0xf) + int(p.c);
            int hi = ((a >> 4) & 0xf) + ((m >> 4) & 0xf);
            if (lo > 9) { lo += 6; hi++; }
            uint8_t result_hi_pre = uint8_t(hi << 4);
            p.v = (~(a ^ m) & (a ^ result_hi_pre) & 0x80) != 0;
            if (hi > 9) { hi += 6; p.c = true; } else { p.c = false; }
            uint8_t result = uint8_t((hi << 4) | (lo & 0xf));
            set_nz8(result);
            a = uint16_t((a & 0xff00) | result);
        } else {
            int t = int(uint8_t(a)) + int(m) + int(p.c);
            p.v = (~(a ^ m) & (a ^ t) & 0x80) != 0;
            p.c = t > 0xff;
            uint8_t result = uint8_t(t);
            set_nz8(result);
            a = uint16_t((a & 0xff00) | result);
        }
    } else {
        uint16_t m = uint16_t(read(addr) | (read(addr + 1) << 8));
        if (p.d) {
            int result = 0, carry = int(p.c);
            for (int nib = 0; nib < 4; nib++) {
                int shift = nib * 4;
                int da = (a >> shift) & 0xf, dm = (m >> shift) & 0xf;
                int t = da + dm + carry;
                carry = 0;
                if (t > 9) { t += 6; carry = 1; }
                result |= (t & 0xf) << shift;
            }
            p.v = (~(a ^ m) & (a ^ uint16_t(result)) & 0x8000) != 0;
            p.c = carry != 0;
            set_nz16(uint16_t(result));
            a = uint16_t(result);
        } else {
            int t = int(a) + int(m) + int(p.c);
            p.v = (~(a ^ m) & (a ^ uint16_t(t)) & 0x8000) != 0;
            p.c = t > 0xffff;
            set_nz16(uint16_t(t));
            a = uint16_t(t);
        }
    }
}
void W65C816::op_sbc(uint32_t addr) {
    if (p.m) {
        uint8_t m = read(addr);
        int t = int(uint8_t(a)) - int(m) - int(!p.c);
        p.v = ((a ^ m) & (a ^ t) & 0x80) != 0;
        p.c = t >= 0;
        if (p.d) {
            int borrow = int(!p.c ? 1 : 0);
            borrow = t >= 0 ? (int(uint8_t(a)) - int(m) - int(!p.c) < 0 ? 1 : 0) : 1;
            borrow = int(!p.c);
            int lo = (a & 0xf) - (m & 0xf) - borrow;
            int hi = ((a >> 4) & 0xf) - ((m >> 4) & 0xf);
            if (lo < 0) { lo -= 6; hi--; }
            if (hi < 0) hi -= 6;
            uint8_t result = uint8_t((hi << 4) | (lo & 0xf));
            set_nz8(uint8_t(t));
            a = uint16_t((a & 0xff00) | result);
        } else {
            uint8_t result = uint8_t(t);
            set_nz8(result);
            a = uint16_t((a & 0xff00) | result);
        }
    } else {
        uint16_t m = uint16_t(read(addr) | (read(addr + 1) << 8));
        int t = int(a) - int(m) - int(!p.c);
        p.v = ((a ^ m) & (a ^ t) & 0x8000) != 0;
        p.c = t >= 0;
        if (p.d) {
            int result = 0, borrow = int(!p.c);
            for (int nib = 0; nib < 4; nib++) {
                int shift = nib * 4;
                int da = (a >> shift) & 0xf, dm = (m >> shift) & 0xf;
                int r = da - dm - borrow;
                borrow = 0;
                if (r < 0) { r -= 6; borrow = 1; }
                result |= (r & 0xf) << shift;
            }
            set_nz16(uint16_t(t));
            a = uint16_t(result);
        } else {
            set_nz16(uint16_t(t));
            a = uint16_t(t);
        }
    }
}
void W65C816::op_and(uint32_t addr) {
    if (p.m) {
        uint8_t v = uint8_t(uint8_t(a) & read(addr));
        set_nz8(v);
        a = uint16_t((a & 0xff00) | v);
    } else {
        uint16_t v = uint16_t(a & uint16_t(read(addr) | (read(addr + 1) << 8)));
        set_nz16(v);
        a = v;
    }
}
void W65C816::op_ora(uint32_t addr) {
    if (p.m) {
        uint8_t v = uint8_t(uint8_t(a) | read(addr));
        set_nz8(v);
        a = uint16_t((a & 0xff00) | v);
    } else {
        uint16_t v = uint16_t(a | uint16_t(read(addr) | (read(addr + 1) << 8)));
        set_nz16(v);
        a = v;
    }
}
void W65C816::op_eor(uint32_t addr) {
    if (p.m) {
        uint8_t v = uint8_t(uint8_t(a) ^ read(addr));
        set_nz8(v);
        a = uint16_t((a & 0xff00) | v);
    } else {
        uint16_t v = uint16_t(a ^ uint16_t(read(addr) | (read(addr + 1) << 8)));
        set_nz16(v);
        a = v;
    }
}
void W65C816::op_bit(uint32_t addr, bool immediate) {
    if (p.m) {
        uint8_t m = read(addr);
        uint8_t r = uint8_t(uint8_t(a) & m);
        p.z = r == 0;
        if (!immediate) {
            p.n = (m & 0x80) != 0;
            p.v = (m & 0x40) != 0;
        }
    } else {
        uint16_t m = uint16_t(read(addr) | (read(addr + 1) << 8));
        uint16_t r = uint16_t(a & m);
        p.z = r == 0;
        if (!immediate) {
            p.n = (m & 0x8000) != 0;
            p.v = (m & 0x4000) != 0;
        }
    }
}
void W65C816::op_cmp(uint32_t addr) {
    if (p.m) {
        int t = int(uint8_t(a)) - int(read(addr));
        p.c = t >= 0;
        set_nz8(uint8_t(t));
    } else {
        int t = int(a) - int(uint16_t(read(addr) | (read(addr + 1) << 8)));
        p.c = t >= 0;
        set_nz16(uint16_t(t));
    }
}
void W65C816::op_cpx(uint32_t addr) {
    if (p.x) {
        int t = int(uint8_t(x)) - int(read(addr));
        p.c = t >= 0;
        set_nz8(uint8_t(t));
    } else {
        int t = int(x) - int(uint16_t(read(addr) | (read(addr + 1) << 8)));
        p.c = t >= 0;
        set_nz16(uint16_t(t));
    }
}
void W65C816::op_cpy(uint32_t addr) {
    if (p.x) {
        int t = int(uint8_t(y)) - int(read(addr));
        p.c = t >= 0;
        set_nz8(uint8_t(t));
    } else {
        int t = int(y) - int(uint16_t(read(addr) | (read(addr + 1) << 8)));
        p.c = t >= 0;
        set_nz16(uint16_t(t));
    }
}
void W65C816::op_lda(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        set_nz8(v);
        a = uint16_t((a & 0xff00) | v);
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        set_nz16(v);
        a = v;
    }
}
void W65C816::op_ldx(uint32_t addr) {
    if (p.x) {
        uint8_t v = read(addr);
        set_nz8(v);
        x = v;
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        set_nz16(v);
        x = v;
    }
}
void W65C816::op_ldy(uint32_t addr) {
    if (p.x) {
        uint8_t v = read(addr);
        set_nz8(v);
        y = v;
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        set_nz16(v);
        y = v;
    }
}
void W65C816::op_sta(uint32_t addr) {
    if (p.m) {
        write(addr, uint8_t(a));
    } else {
        write(addr, uint8_t(a));
        write(addr + 1, uint8_t(a >> 8));
    }
}
void W65C816::op_stx(uint32_t addr) {
    if (p.x) {
        write(addr, uint8_t(x));
    } else {
        write(addr, uint8_t(x));
        write(addr + 1, uint8_t(x >> 8));
    }
}
void W65C816::op_sty(uint32_t addr) {
    if (p.x) {
        write(addr, uint8_t(y));
    } else {
        write(addr, uint8_t(y));
        write(addr + 1, uint8_t(y >> 8));
    }
}
void W65C816::op_stz(uint32_t addr) {
    if (p.m) {
        write(addr, 0);
    } else {
        write(addr, 0);
        write(addr + 1, 0);
    }
}
void W65C816::op_asl_mem(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        p.c = (v & 0x80) != 0;
        v = uint8_t(v << 1);
        set_nz8(v);
        write(addr, v);
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        p.c = (v & 0x8000) != 0;
        v = uint16_t(v << 1);
        set_nz16(v);
        write(addr, uint8_t(v));
        write(addr + 1, uint8_t(v >> 8));
    }
}
void W65C816::op_lsr_mem(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        p.c = (v & 1) != 0;
        v = uint8_t(v >> 1);
        set_nz8(v);
        write(addr, v);
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        p.c = (v & 1) != 0;
        v = uint16_t(v >> 1);
        set_nz16(v);
        write(addr, uint8_t(v));
        write(addr + 1, uint8_t(v >> 8));
    }
}
void W65C816::op_rol_mem(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        bool c = (v & 0x80) != 0;
        v = uint8_t((v << 1) | uint8_t(p.c));
        p.c = c;
        set_nz8(v);
        write(addr, v);
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        bool c = (v & 0x8000) != 0;
        v = uint16_t((v << 1) | uint16_t(p.c));
        p.c = c;
        set_nz16(v);
        write(addr, uint8_t(v));
        write(addr + 1, uint8_t(v >> 8));
    }
}
void W65C816::op_ror_mem(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        bool c = (v & 1) != 0;
        v = uint8_t((v >> 1) | (uint8_t(p.c) << 7));
        p.c = c;
        set_nz8(v);
        write(addr, v);
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        bool c = (v & 1) != 0;
        v = uint16_t((v >> 1) | (uint16_t(p.c) << 15));
        p.c = c;
        set_nz16(v);
        write(addr, uint8_t(v));
        write(addr + 1, uint8_t(v >> 8));
    }
}
void W65C816::op_inc_mem(uint32_t addr) {
    if (p.m) {
        uint8_t v = uint8_t(read(addr) + 1);
        set_nz8(v);
        write(addr, v);
    } else {
        uint16_t v = uint16_t(uint16_t(read(addr) | (read(addr + 1) << 8)) + 1);
        set_nz16(v);
        write(addr, uint8_t(v));
        write(addr + 1, uint8_t(v >> 8));
    }
}
void W65C816::op_dec_mem(uint32_t addr) {
    if (p.m) {
        uint8_t v = uint8_t(read(addr) - 1);
        set_nz8(v);
        write(addr, v);
    } else {
        uint16_t v = uint16_t(uint16_t(read(addr) | (read(addr + 1) << 8)) - 1);
        set_nz16(v);
        write(addr, uint8_t(v));
        write(addr + 1, uint8_t(v >> 8));
    }
}
void W65C816::op_trb(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        p.z = (v & uint8_t(a)) == 0;
        write(addr, uint8_t(v & ~uint8_t(a)));
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        p.z = (v & a) == 0;
        uint16_t r = uint16_t(v & ~a);
        write(addr, uint8_t(r));
        write(addr + 1, uint8_t(r >> 8));
    }
}
void W65C816::op_tsb(uint32_t addr) {
    if (p.m) {
        uint8_t v = read(addr);
        p.z = (v & uint8_t(a)) == 0;
        write(addr, uint8_t(v | uint8_t(a)));
    } else {
        uint16_t v = uint16_t(read(addr) | (read(addr + 1) << 8));
        p.z = (v & a) == 0;
        uint16_t r = uint16_t(v | a);
        write(addr, uint8_t(r));
        write(addr + 1, uint8_t(r >> 8));
    }
}
void W65C816::branch(bool condition) {
    int8_t off = int8_t(fetch8());
    if (condition) {
        extra_cycles_ += 1;
        pc_ = uint16_t(pc_ + off);
    }
}

int W65C816::take_irq(uint32_t vector_native, uint32_t vector_emulated) {
    if (e_) {
        push16(pc_);
        push8(uint8_t(get_p() & ~0x10));  // B flag clear: this is a real interrupt, not BRK
    } else {
        push8(pbr);
        push16(pc_);
        push8(get_p());
    }
    p.i = true;
    p.d = false;
    pbr = 0;
    uint32_t vector = e_ ? vector_emulated : vector_native;
    pc_ = uint16_t(read(vector) | (read(vector + 1) << 8));
    return e_ ? 7 : 8;
}

// ---------------------------------------------------------------------------
// Immediate-operand helper: fetches a 1-byte or 2-byte immediate operand
// (per `wide`) as an address pointing at the just-fetched bytes in the
// instruction stream, matching what op_* helpers expect.
// ---------------------------------------------------------------------------
namespace {}  // (no anonymous helpers needed)

int W65C816::run(int cycles) {
    int executed = 0;
    while (executed < cycles) {
        extra_cycles_ = 0;

        if (nmi_request_ != IrqLine::Clear && nmi_state_ == IrqLine::Clear) {
            nmi_state_ = IrqLine::Assert;
            waiting_ = stopped_ = false;
            extra_cycles_ += take_irq(0xffea, 0xfffa);
            if (nmi_request_ == IrqLine::Pulse) nmi_request_ = IrqLine::Clear;
        } else if (irq_request_ != IrqLine::Clear && !p.i) {
            waiting_ = stopped_ = false;
            extra_cycles_ += take_irq(0xffee, 0xfffe);
            if (irq_request_ == IrqLine::Pulse) irq_request_ = IrqLine::Clear;
        }

        if (stopped_ || waiting_) {
            int step = 2;
            if (cycle_handler_) cycle_handler_(step);
            executed += step;
            continue;
        }

        int addr_extra = 0;
        if (fetch_hook_) fetch_hook_(pc());
        uint8_t op = fetch8();
        int base_cycles = 2;

        // Immediate operand address: points at the operand byte(s) just
        // ahead of pc_, then advances pc_ past them (1 byte in 8-bit mode,
        // 2 in 16-bit mode).
        auto imm_addr = [this](bool wide) -> uint32_t {
            uint32_t a2 = (uint32_t(pbr) << 16) | pc_;
            pc_ = uint16_t(pc_ + (wide ? 2 : 1));
            return a2;
        };

        switch (op) {
            // --- Control / stack / mode ---
            case 0x00: fetch8(); extra_cycles_ += take_irq(0xffe6, 0xfffe); base_cycles = e_ ? 7 : 8; break;  // BRK
            case 0x02: fetch8(); base_cycles = 7; break;                                                     // COP
            case 0xdb: stopped_ = true; base_cycles = 3; break;                                               // STP
            case 0xcb: waiting_ = true; base_cycles = 3; break;                                               // WAI
            case 0xea: base_cycles = 2; break;                                                                // NOP
            case 0x42: fetch8(); base_cycles = 2; break;                                                      // WDM

            case 0x18: p.c = false; base_cycles = 2; break;  // CLC
            case 0x38: p.c = true; base_cycles = 2; break;   // SEC
            case 0x58: p.i = false; base_cycles = 2; break;  // CLI
            case 0x78: p.i = true; base_cycles = 2; break;   // SEI
            case 0xb8: p.v = false; base_cycles = 2; break;  // CLV
            case 0xd8: p.d = false; base_cycles = 2; break;  // CLD
            case 0xf8: p.d = true; base_cycles = 2; break;   // SED

            case 0xfb: {  // XCE
                bool old_e = e_;
                e_ = p.c;
                p.c = old_e;
                if (e_) {
                    p.m = true;
                    p.x = true;
                    sp = uint16_t(0x100 | (sp & 0xff));
                } else if (p.x) {
                    x &= 0xff;
                    y &= 0xff;
                }
                base_cycles = 2;
                break;
            }
            case 0xc2: set_p(uint8_t(get_p() & ~fetch8())); base_cycles = 3; break;  // REP
            case 0xe2: set_p(uint8_t(get_p() | fetch8())); base_cycles = 3; break;   // SEP

            case 0x08: push8(get_p()); base_cycles = 3; break;  // PHP
            case 0x28: set_p(pop8()); base_cycles = 4; break;   // PLP

            case 0x48:  // PHA
                if (p.m) { push8(uint8_t(a)); base_cycles = 3; } else { push16(a); base_cycles = 4; }
                break;
            case 0x68:  // PLA
                if (p.m) { uint8_t v = pop8(); set_nz8(v); a = uint16_t((a & 0xff00) | v); base_cycles = 4; }
                else { uint16_t v = pop16(); set_nz16(v); a = v; base_cycles = 5; }
                break;
            case 0xda:  // PHX
                if (p.x) { push8(uint8_t(x)); base_cycles = 3; } else { push16(x); base_cycles = 4; }
                break;
            case 0xfa:  // PLX
                if (p.x) { uint8_t v = pop8(); set_nz8(v); x = v; base_cycles = 4; }
                else { uint16_t v = pop16(); set_nz16(v); x = v; base_cycles = 5; }
                break;
            case 0x5a:  // PHY
                if (p.x) { push8(uint8_t(y)); base_cycles = 3; } else { push16(y); base_cycles = 4; }
                break;
            case 0x7a:  // PLY
                if (p.x) { uint8_t v = pop8(); set_nz8(v); y = v; base_cycles = 4; }
                else { uint16_t v = pop16(); set_nz16(v); y = v; base_cycles = 5; }
                break;
            case 0x8b: push8(dbr); base_cycles = 3; break;                        // PHB
            case 0xab: dbr = pop8(); set_nz8(dbr); base_cycles = 4; break;        // PLB
            case 0x4b: push8(pbr); base_cycles = 3; break;                       // PHK
            case 0x0b: push16(d); base_cycles = 4; break;                        // PHD
            case 0x2b: d = pop16(); set_nz16(d); base_cycles = 5; break;         // PLD
            case 0xf4: push16(fetch16()); base_cycles = 5; break;                // PEA
            case 0xd4: {  // PEI
                uint32_t a2 = addr_direct(addr_extra);
                push16(uint16_t(read(a2) | (read(a2 + 1) << 8)));
                base_cycles = 6;
                break;
            }
            case 0x62: {  // PER
                int16_t off = int16_t(fetch16());
                push16(uint16_t(pc_ + off));
                base_cycles = 6;
                break;
            }

            case 0x1b: sp = a; base_cycles = 2; break;  // TCS
            case 0x3b:  // TSC
                a = e_ ? uint16_t(0x100 | (sp & 0xff)) : sp;
                set_nz16(a);
                base_cycles = 2;
                break;
            case 0x5b: d = a; base_cycles = 2; break;                    // TCD
            case 0x7b: a = d; set_nz16(a); base_cycles = 2; break;       // TDC
            case 0xeb: {  // XBA
                uint8_t lo = uint8_t(a), hi = uint8_t(a >> 8);
                a = uint16_t((lo << 8) | hi);
                set_nz8(uint8_t(a));
                base_cycles = 3;
                break;
            }

            // --- Register transfers ---
            case 0xaa:  // TAX
                x = p.x ? uint16_t(uint8_t(a)) : a;
                p.x ? set_nz8(uint8_t(x)) : set_nz16(x);
                base_cycles = 2;
                break;
            case 0xa8:  // TAY
                y = p.x ? uint16_t(uint8_t(a)) : a;
                p.x ? set_nz8(uint8_t(y)) : set_nz16(y);
                base_cycles = 2;
                break;
            case 0x8a:  // TXA
                a = p.m ? uint16_t((a & 0xff00) | uint8_t(x)) : x;
                p.m ? set_nz8(uint8_t(a)) : set_nz16(a);
                base_cycles = 2;
                break;
            case 0x98:  // TYA
                a = p.m ? uint16_t((a & 0xff00) | uint8_t(y)) : y;
                p.m ? set_nz8(uint8_t(a)) : set_nz16(a);
                base_cycles = 2;
                break;
            case 0xba:  // TSX
                x = p.x ? uint16_t(uint8_t(sp)) : sp;
                p.x ? set_nz8(uint8_t(x)) : set_nz16(x);
                base_cycles = 2;
                break;
            case 0x9a:  // TXS
                sp = e_ ? uint16_t(0x100 | uint8_t(x)) : (p.x ? uint16_t(uint8_t(x)) : x);
                base_cycles = 2;
                break;
            case 0x9b:  // TXY
                y = p.x ? uint16_t(uint8_t(x)) : x;
                p.x ? set_nz8(uint8_t(y)) : set_nz16(y);
                base_cycles = 2;
                break;
            case 0xbb:  // TYX
                x = p.x ? uint16_t(uint8_t(y)) : y;
                p.x ? set_nz8(uint8_t(x)) : set_nz16(x);
                base_cycles = 2;
                break;

            // --- Increments / decrements ---
            case 0xc8:  // INY
                y = p.x ? uint16_t(uint8_t(y + 1)) : uint16_t(y + 1);
                p.x ? set_nz8(uint8_t(y)) : set_nz16(y);
                base_cycles = 2;
                break;
            case 0xe8:  // INX
                x = p.x ? uint16_t(uint8_t(x + 1)) : uint16_t(x + 1);
                p.x ? set_nz8(uint8_t(x)) : set_nz16(x);
                base_cycles = 2;
                break;
            case 0x88:  // DEY
                y = p.x ? uint16_t(uint8_t(y - 1)) : uint16_t(y - 1);
                p.x ? set_nz8(uint8_t(y)) : set_nz16(y);
                base_cycles = 2;
                break;
            case 0xca:  // DEX
                x = p.x ? uint16_t(uint8_t(x - 1)) : uint16_t(x - 1);
                p.x ? set_nz8(uint8_t(x)) : set_nz16(x);
                base_cycles = 2;
                break;
            case 0x1a:  // INC A
                if (p.m) { uint8_t v = uint8_t(uint8_t(a) + 1); set_nz8(v); a = uint16_t((a & 0xff00) | v); }
                else { a = uint16_t(a + 1); set_nz16(a); }
                base_cycles = 2;
                break;
            case 0x3a:  // DEC A
                if (p.m) { uint8_t v = uint8_t(uint8_t(a) - 1); set_nz8(v); a = uint16_t((a & 0xff00) | v); }
                else { a = uint16_t(a - 1); set_nz16(a); }
                base_cycles = 2;
                break;

            // --- Shifts/rotates on the accumulator ---
            case 0x0a:  // ASL A
                if (p.m) { uint8_t v = uint8_t(a); p.c = (v & 0x80) != 0; v = uint8_t(v << 1); set_nz8(v); a = uint16_t((a & 0xff00) | v); }
                else { p.c = (a & 0x8000) != 0; a = uint16_t(a << 1); set_nz16(a); }
                base_cycles = 2;
                break;
            case 0x4a:  // LSR A
                if (p.m) { uint8_t v = uint8_t(a); p.c = (v & 1) != 0; v = uint8_t(v >> 1); set_nz8(v); a = uint16_t((a & 0xff00) | v); }
                else { p.c = (a & 1) != 0; a = uint16_t(a >> 1); set_nz16(a); }
                base_cycles = 2;
                break;
            case 0x2a:  // ROL A
                if (p.m) { uint8_t v = uint8_t(a); bool c = (v & 0x80) != 0; v = uint8_t((v << 1) | uint8_t(p.c)); p.c = c; set_nz8(v); a = uint16_t((a & 0xff00) | v); }
                else { bool c = (a & 0x8000) != 0; a = uint16_t((a << 1) | uint16_t(p.c)); p.c = c; set_nz16(a); }
                base_cycles = 2;
                break;
            case 0x6a:  // ROR A
                if (p.m) { uint8_t v = uint8_t(a); bool c = (v & 1) != 0; v = uint8_t((v >> 1) | (uint8_t(p.c) << 7)); p.c = c; set_nz8(v); a = uint16_t((a & 0xff00) | v); }
                else { bool c = (a & 1) != 0; a = uint16_t((a >> 1) | (uint16_t(p.c) << 15)); p.c = c; set_nz16(a); }
                base_cycles = 2;
                break;

            // --- Branches ---
            case 0x10: branch(!p.n); base_cycles = 2; break;  // BPL
            case 0x30: branch(p.n); base_cycles = 2; break;   // BMI
            case 0x50: branch(!p.v); base_cycles = 2; break;  // BVC
            case 0x70: branch(p.v); base_cycles = 2; break;   // BVS
            case 0x90: branch(!p.c); base_cycles = 2; break;  // BCC
            case 0xb0: branch(p.c); base_cycles = 2; break;   // BCS
            case 0xd0: branch(!p.z); base_cycles = 2; break;  // BNE
            case 0xf0: branch(p.z); base_cycles = 2; break;   // BEQ
            case 0x80: { int8_t off = int8_t(fetch8()); pc_ = uint16_t(pc_ + off); base_cycles = 3; break; }    // BRA
            case 0x82: { int16_t off = int16_t(fetch16()); pc_ = uint16_t(pc_ + off); base_cycles = 4; break; }  // BRL

            // --- Jumps / calls / returns ---
            case 0x4c: pc_ = uint16_t(addr_absolute()); base_cycles = 3; break;  // JMP abs
            case 0x6c: {  // JMP (abs)
                uint16_t ptr = fetch16();
                pc_ = uint16_t(read(ptr) | (read(uint16_t(ptr + 1)) << 8));
                base_cycles = 5;
                break;
            }
            case 0x7c: {  // JMP (abs,X)
                uint16_t ptr = uint16_t(fetch16() + x);
                pc_ = uint16_t(read((uint32_t(pbr) << 16) | ptr) |
                               (read((uint32_t(pbr) << 16) | uint16_t(ptr + 1)) << 8));
                base_cycles = 6;
                break;
            }
            case 0xdc: {  // JMP [abs]
                uint16_t ptr = fetch16();
                pc_ = uint16_t(read(ptr) | (read(uint16_t(ptr + 1)) << 8));
                pbr = read(uint16_t(ptr + 2));
                base_cycles = 6;
                break;
            }
            case 0x5c: {  // JMP long
                uint32_t a2 = addr_absolute_long();
                pc_ = uint16_t(a2);
                pbr = uint8_t(a2 >> 16);
                base_cycles = 4;
                break;
            }
            case 0x20: {  // JSR abs
                uint16_t target = fetch16();
                push16(uint16_t(pc_ - 1));
                pc_ = target;
                base_cycles = 6;
                break;
            }
            case 0xfc: {  // JSR (abs,X)
                uint16_t target = fetch16();
                push16(uint16_t(pc_ - 1));
                uint16_t ptr = uint16_t(target + x);
                pc_ = uint16_t(read((uint32_t(pbr) << 16) | ptr) |
                               (read((uint32_t(pbr) << 16) | uint16_t(ptr + 1)) << 8));
                base_cycles = 8;
                break;
            }
            case 0x22: {  // JSL long
                uint32_t target = fetch24();
                push8(pbr);
                push16(uint16_t(pc_ - 1));
                pbr = uint8_t(target >> 16);
                pc_ = uint16_t(target);
                base_cycles = 8;
                break;
            }
            case 0x60: pc_ = uint16_t(pop16() + 1); base_cycles = 6; break;                    // RTS
            case 0x6b: pc_ = uint16_t(pop16() + 1); pbr = pop8(); base_cycles = 6; break;       // RTL
            case 0x40:  // RTI
                set_p(pop8());
                pc_ = pop16();
                if (!e_) pbr = pop8();
                base_cycles = e_ ? 6 : 7;
                break;

            // --- Block move ---
            case 0x54: {  // MVP (decrementing)
                uint8_t dst_bank = fetch8(), src_bank = fetch8();
                write((uint32_t(dst_bank) << 16) | y, read((uint32_t(src_bank) << 16) | x));
                x = uint16_t(x - 1);
                y = uint16_t(y - 1);
                a = uint16_t(a - 1);
                dbr = dst_bank;
                if (a != 0xffff) pc_ = uint16_t(pc_ - 3);
                base_cycles = 7;
                break;
            }
            case 0x44: {  // MVN (incrementing)
                uint8_t dst_bank = fetch8(), src_bank = fetch8();
                write((uint32_t(dst_bank) << 16) | y, read((uint32_t(src_bank) << 16) | x));
                x = uint16_t(x + 1);
                y = uint16_t(y + 1);
                a = uint16_t(a - 1);
                dbr = dst_bank;
                if (a != 0xffff) pc_ = uint16_t(pc_ - 3);
                base_cycles = 7;
                break;
            }

            // --- BIT / TRB / TSB ---
            case 0x24: op_bit(addr_direct(addr_extra), false); base_cycles = 3; break;
            case 0x2c: op_bit(addr_absolute(), false); base_cycles = 4; break;
            case 0x34: op_bit(addr_direct_x(addr_extra), false); base_cycles = 4; break;
            case 0x3c: op_bit(addr_absolute_x(addr_extra), false); base_cycles = 4; break;
            case 0x89: op_bit(imm_addr(!p.m), true); base_cycles = 3; break;
            case 0x14: op_tsb(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0x0c: op_tsb(addr_absolute()); base_cycles = 6; break;
            case 0x04: op_trb(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0x1c: op_trb(addr_absolute()); base_cycles = 6; break;

            // --- STZ ---
            case 0x64: op_stz(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x74: op_stz(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0x9c: op_stz(addr_absolute()); base_cycles = 4; break;
            case 0x9e: op_stz(addr_absolute_x(addr_extra)); base_cycles = 5; break;

            // --- Shifts/rotates/inc/dec in memory ---
            case 0x06: op_asl_mem(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0x0e: op_asl_mem(addr_absolute()); base_cycles = 6; break;
            case 0x16: op_asl_mem(addr_direct_x(addr_extra)); base_cycles = 6; break;
            case 0x1e: op_asl_mem(addr_absolute_x(addr_extra)); base_cycles = 7; break;
            case 0x46: op_lsr_mem(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0x4e: op_lsr_mem(addr_absolute()); base_cycles = 6; break;
            case 0x56: op_lsr_mem(addr_direct_x(addr_extra)); base_cycles = 6; break;
            case 0x5e: op_lsr_mem(addr_absolute_x(addr_extra)); base_cycles = 7; break;
            case 0x26: op_rol_mem(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0x2e: op_rol_mem(addr_absolute()); base_cycles = 6; break;
            case 0x36: op_rol_mem(addr_direct_x(addr_extra)); base_cycles = 6; break;
            case 0x3e: op_rol_mem(addr_absolute_x(addr_extra)); base_cycles = 7; break;
            case 0x66: op_ror_mem(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0x6e: op_ror_mem(addr_absolute()); base_cycles = 6; break;
            case 0x76: op_ror_mem(addr_direct_x(addr_extra)); base_cycles = 6; break;
            case 0x7e: op_ror_mem(addr_absolute_x(addr_extra)); base_cycles = 7; break;
            case 0xe6: op_inc_mem(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0xee: op_inc_mem(addr_absolute()); base_cycles = 6; break;
            case 0xf6: op_inc_mem(addr_direct_x(addr_extra)); base_cycles = 6; break;
            case 0xfe: op_inc_mem(addr_absolute_x(addr_extra)); base_cycles = 7; break;
            case 0xc6: op_dec_mem(addr_direct(addr_extra)); base_cycles = 5; break;
            case 0xce: op_dec_mem(addr_absolute()); base_cycles = 6; break;
            case 0xd6: op_dec_mem(addr_direct_x(addr_extra)); base_cycles = 6; break;
            case 0xde: op_dec_mem(addr_absolute_x(addr_extra)); base_cycles = 7; break;

            // --- LDX/LDY/STX/STY/CPX/CPY ---
            case 0xa2: op_ldx(imm_addr(!p.x)); base_cycles = 2; break;
            case 0xa6: op_ldx(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xae: op_ldx(addr_absolute()); base_cycles = 4; break;
            case 0xb6: op_ldx(addr_direct_y(addr_extra)); base_cycles = 4; break;
            case 0xbe: op_ldx(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0xa0: op_ldy(imm_addr(!p.x)); base_cycles = 2; break;
            case 0xa4: op_ldy(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xac: op_ldy(addr_absolute()); base_cycles = 4; break;
            case 0xb4: op_ldy(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0xbc: op_ldy(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0x86: op_stx(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x8e: op_stx(addr_absolute()); base_cycles = 4; break;
            case 0x96: op_stx(addr_direct_y(addr_extra)); base_cycles = 4; break;
            case 0x84: op_sty(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x8c: op_sty(addr_absolute()); base_cycles = 4; break;
            case 0x94: op_sty(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0xc0: op_cpy(imm_addr(!p.x)); base_cycles = 2; break;
            case 0xc4: op_cpy(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xcc: op_cpy(addr_absolute()); base_cycles = 4; break;
            case 0xe0: op_cpx(imm_addr(!p.x)); base_cycles = 2; break;
            case 0xe4: op_cpx(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xec: op_cpx(addr_absolute()); base_cycles = 4; break;

            // --- ORA ---
            case 0x01: op_ora(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0x03: op_ora(addr_stack_relative()); base_cycles = 4; break;
            case 0x05: op_ora(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x07: op_ora(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0x09: op_ora(imm_addr(!p.m)); base_cycles = 2; break;
            case 0x0d: op_ora(addr_absolute()); base_cycles = 4; break;
            case 0x0f: op_ora(addr_absolute_long()); base_cycles = 5; break;
            case 0x11: op_ora(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0x12: op_ora(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0x13: op_ora(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0x15: op_ora(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0x17: op_ora(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0x19: op_ora(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0x1d: op_ora(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0x1f: op_ora(addr_absolute_long_x()); base_cycles = 5; break;

            // --- AND ---
            case 0x21: op_and(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0x23: op_and(addr_stack_relative()); base_cycles = 4; break;
            case 0x25: op_and(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x27: op_and(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0x29: op_and(imm_addr(!p.m)); base_cycles = 2; break;
            case 0x2d: op_and(addr_absolute()); base_cycles = 4; break;
            case 0x2f: op_and(addr_absolute_long()); base_cycles = 5; break;
            case 0x31: op_and(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0x32: op_and(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0x33: op_and(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0x35: op_and(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0x37: op_and(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0x39: op_and(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0x3d: op_and(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0x3f: op_and(addr_absolute_long_x()); base_cycles = 5; break;

            // --- EOR ---
            case 0x41: op_eor(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0x43: op_eor(addr_stack_relative()); base_cycles = 4; break;
            case 0x45: op_eor(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x47: op_eor(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0x49: op_eor(imm_addr(!p.m)); base_cycles = 2; break;
            case 0x4d: op_eor(addr_absolute()); base_cycles = 4; break;
            case 0x4f: op_eor(addr_absolute_long()); base_cycles = 5; break;
            case 0x51: op_eor(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0x52: op_eor(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0x53: op_eor(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0x55: op_eor(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0x57: op_eor(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0x59: op_eor(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0x5d: op_eor(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0x5f: op_eor(addr_absolute_long_x()); base_cycles = 5; break;

            // --- ADC ---
            case 0x61: op_adc(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0x63: op_adc(addr_stack_relative()); base_cycles = 4; break;
            case 0x65: op_adc(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x67: op_adc(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0x69: op_adc(imm_addr(!p.m)); base_cycles = 2; break;
            case 0x6d: op_adc(addr_absolute()); base_cycles = 4; break;
            case 0x6f: op_adc(addr_absolute_long()); base_cycles = 5; break;
            case 0x71: op_adc(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0x72: op_adc(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0x73: op_adc(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0x75: op_adc(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0x77: op_adc(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0x79: op_adc(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0x7d: op_adc(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0x7f: op_adc(addr_absolute_long_x()); base_cycles = 5; break;

            // --- STA ---
            case 0x81: op_sta(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0x83: op_sta(addr_stack_relative()); base_cycles = 4; break;
            case 0x85: op_sta(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0x87: op_sta(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0x8d: op_sta(addr_absolute()); base_cycles = 4; break;
            case 0x8f: op_sta(addr_absolute_long()); base_cycles = 5; break;
            case 0x91: op_sta(addr_direct_indirect_y(addr_extra)); base_cycles = 6; break;
            case 0x92: op_sta(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0x93: op_sta(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0x95: op_sta(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0x97: op_sta(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0x99: op_sta(addr_absolute_y(addr_extra)); base_cycles = 5; break;
            case 0x9d: op_sta(addr_absolute_x(addr_extra)); base_cycles = 5; break;
            case 0x9f: op_sta(addr_absolute_long_x()); base_cycles = 5; break;

            // --- LDA ---
            case 0xa1: op_lda(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0xa3: op_lda(addr_stack_relative()); base_cycles = 4; break;
            case 0xa5: op_lda(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xa7: op_lda(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0xa9: op_lda(imm_addr(!p.m)); base_cycles = 2; break;
            case 0xad: op_lda(addr_absolute()); base_cycles = 4; break;
            case 0xaf: op_lda(addr_absolute_long()); base_cycles = 5; break;
            case 0xb1: op_lda(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0xb2: op_lda(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0xb3: op_lda(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0xb5: op_lda(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0xb7: op_lda(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0xb9: op_lda(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0xbd: op_lda(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0xbf: op_lda(addr_absolute_long_x()); base_cycles = 5; break;

            // --- CMP ---
            case 0xc1: op_cmp(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0xc3: op_cmp(addr_stack_relative()); base_cycles = 4; break;
            case 0xc5: op_cmp(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xc7: op_cmp(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0xc9: op_cmp(imm_addr(!p.m)); base_cycles = 2; break;
            case 0xcd: op_cmp(addr_absolute()); base_cycles = 4; break;
            case 0xcf: op_cmp(addr_absolute_long()); base_cycles = 5; break;
            case 0xd1: op_cmp(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0xd2: op_cmp(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0xd3: op_cmp(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0xd5: op_cmp(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0xd7: op_cmp(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0xd9: op_cmp(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0xdd: op_cmp(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0xdf: op_cmp(addr_absolute_long_x()); base_cycles = 5; break;

            // --- SBC ---
            case 0xe1: op_sbc(addr_direct_indirect_x(addr_extra)); base_cycles = 6; break;
            case 0xe3: op_sbc(addr_stack_relative()); base_cycles = 4; break;
            case 0xe5: op_sbc(addr_direct(addr_extra)); base_cycles = 3; break;
            case 0xe7: op_sbc(addr_direct_indirect_long(addr_extra)); base_cycles = 6; break;
            case 0xe9: op_sbc(imm_addr(!p.m)); base_cycles = 2; break;
            case 0xed: op_sbc(addr_absolute()); base_cycles = 4; break;
            case 0xef: op_sbc(addr_absolute_long()); base_cycles = 5; break;
            case 0xf1: op_sbc(addr_direct_indirect_y(addr_extra)); base_cycles = 5; break;
            case 0xf2: op_sbc(addr_direct_indirect(addr_extra)); base_cycles = 5; break;
            case 0xf3: op_sbc(addr_stack_relative_indirect_y()); base_cycles = 7; break;
            case 0xf5: op_sbc(addr_direct_x(addr_extra)); base_cycles = 4; break;
            case 0xf7: op_sbc(addr_direct_indirect_long_y(addr_extra)); base_cycles = 6; break;
            case 0xf9: op_sbc(addr_absolute_y(addr_extra)); base_cycles = 4; break;
            case 0xfd: op_sbc(addr_absolute_x(addr_extra)); base_cycles = 4; break;
            case 0xff: op_sbc(addr_absolute_long_x()); base_cycles = 5; break;

            default: break;  // unreachable: every opcode above is covered
        }

        int step = base_cycles + addr_extra + extra_cycles_;
        // Native-mode 16-bit ALU/RMW ops each need one extra cycle over the
        // 8-bit base cost tabulated above; approximate this uniformly.
        if (!p.m) {
            switch (op) {
                case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0d: case 0x0f:
                case 0x11: case 0x12: case 0x13: case 0x15: case 0x17: case 0x19: case 0x1d: case 0x1f:
                case 0x21: case 0x23: case 0x25: case 0x27: case 0x29: case 0x2d: case 0x2f:
                case 0x31: case 0x32: case 0x33: case 0x35: case 0x37: case 0x39: case 0x3d: case 0x3f:
                case 0x41: case 0x43: case 0x45: case 0x47: case 0x49: case 0x4d: case 0x4f:
                case 0x51: case 0x52: case 0x53: case 0x55: case 0x57: case 0x59: case 0x5d: case 0x5f:
                case 0x61: case 0x63: case 0x65: case 0x67: case 0x69: case 0x6d: case 0x6f:
                case 0x71: case 0x72: case 0x73: case 0x75: case 0x77: case 0x79: case 0x7d: case 0x7f:
                case 0x81: case 0x83: case 0x85: case 0x87: case 0x8d: case 0x8f:
                case 0x91: case 0x92: case 0x93: case 0x95: case 0x97: case 0x99: case 0x9d: case 0x9f:
                case 0xa1: case 0xa3: case 0xa5: case 0xa7: case 0xa9: case 0xad: case 0xaf:
                case 0xb1: case 0xb2: case 0xb3: case 0xb5: case 0xb7: case 0xb9: case 0xbd: case 0xbf:
                case 0xc1: case 0xc3: case 0xc5: case 0xc7: case 0xc9: case 0xcd: case 0xcf:
                case 0xd1: case 0xd2: case 0xd3: case 0xd5: case 0xd7: case 0xd9: case 0xdd: case 0xdf:
                case 0xe1: case 0xe3: case 0xe5: case 0xe7: case 0xe9: case 0xed: case 0xef:
                case 0xf1: case 0xf2: case 0xf3: case 0xf5: case 0xf7: case 0xf9: case 0xfd: case 0xff:
                case 0x06: case 0x0e: case 0x16: case 0x1e: case 0x46: case 0x4e: case 0x56: case 0x5e:
                case 0x26: case 0x2e: case 0x36: case 0x3e: case 0x66: case 0x6e: case 0x76: case 0x7e:
                case 0xe6: case 0xee: case 0xf6: case 0xfe: case 0xc6: case 0xce: case 0xd6: case 0xde:
                case 0x14: case 0x0c: case 0x04: case 0x1c: case 0x64: case 0x74: case 0x9c: case 0x9e:
                case 0x24: case 0x2c: case 0x34: case 0x3c: case 0x89:
                    step += 1;
                    break;
                default:
                    break;
            }
        }
        if (!p.x) {
            switch (op) {
                case 0xa2: case 0xa0: case 0xa6: case 0xae: case 0xb6: case 0xbe:
                case 0xa4: case 0xac: case 0xb4: case 0xbc:
                case 0x86: case 0x8e: case 0x96: case 0x84: case 0x8c: case 0x94:
                case 0xc0: case 0xc4: case 0xcc: case 0xe0: case 0xe4: case 0xec:
                    step += 1;
                    break;
                default:
                    break;
            }
        }

        if (cycle_handler_) cycle_handler_(step);
        executed += step;
    }
    return executed;
}

}  // namespace dsp
