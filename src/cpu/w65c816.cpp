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
    x &= 0xff;
    y &= 0xff;
    d = 0;
    dbr = 0;
    pbr = 0;
    sp = uint16_t(0x100 | (sp & 0xff));
    pc_ = read_vector(0xfffc);
    irq_request_ = IrqLine::Clear;
    nmi_request_ = IrqLine::Clear;
    nmi_latched_ = false;
    stopped_ = false;
    waiting_ = false;
}

void W65C816::set_emulation(bool e) {
    e_ = e;
    if (e_) {
        p.m = p.x = true;
        x &= 0xff;
        y &= 0xff;
        sp = uint16_t(0x100 | (sp & 0xff));
    }
}

void W65C816::set_nmi(IrqLine state) {
    nmi_request_ = state;
    if (state == IrqLine::Clear) nmi_latched_ = false;
}

uint8_t W65C816::fetch8() {
    uint8_t v = rd((uint32_t(pbr) << 16) | pc_);
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
    uint8_t v = uint8_t(uint8_t(p.c) | (uint8_t(p.z) << 1) | (uint8_t(p.i) << 2) | (uint8_t(p.d) << 3) |
                        (uint8_t(p.v) << 6) | (uint8_t(p.n) << 7));
    if (e_) {
        v |= 0x30;
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
    if (e_) {
        p.m = p.x = true;
    } else {
        p.x = (value & 0x10) != 0;
        p.m = (value & 0x20) != 0;
    }
    if (p.x) {
        x &= 0xff;
        y &= 0xff;
    }
}

uint32_t W65C816::dp_addr(uint32_t offset) const {
    if (e_ && (d & 0xff) == 0) return uint32_t((d & 0xff00) | (offset & 0xff));
    return uint32_t(uint16_t(d + offset));
}

void W65C816::push8(uint8_t value) {
    wr(sp, value);
    sp = e_ ? uint16_t(0x100 | uint8_t(sp - 1)) : uint16_t(sp - 1);
}
uint8_t W65C816::pull8() {
    sp = e_ ? uint16_t(0x100 | uint8_t(sp + 1)) : uint16_t(sp + 1);
    return rd(sp);
}

uint16_t W65C816::read_vector(uint16_t vector) {
    uint32_t address = vector_handler_ ? vector_handler_(vector) : vector;
    return uint16_t(rd(address) | (rd(address + 1) << 8));
}

void W65C816::interrupt(uint16_t vector_native, uint16_t vector_emulated, bool brk) {
    if (e_) {
        push8(uint8_t(pc_ >> 8));
        push8(uint8_t(pc_));
        uint8_t pv = get_p();
        if (!brk) pv = uint8_t(pv & ~0x10);
        push8(pv);
        cycles_ += 7;
    } else {
        push8(pbr);
        push8(uint8_t(pc_ >> 8));
        push8(uint8_t(pc_));
        push8(get_p());
        cycles_ += 8;
    }
    p.i = true;
    p.d = false;
    pbr = 0;
    pc_ = read_vector(e_ ? vector_emulated : vector_native);
}

// ---------------------------------------------------------------------------
// Addressing
// ---------------------------------------------------------------------------

uint32_t W65C816::ea(Mode mode, bool wide, bool is_write) {
    bank0_mode_ = false;
    const bool dl = (d & 0xff) != 0;
    switch (mode) {
        case Mode::Imm: {
            uint32_t address = (uint32_t(pbr) << 16) | pc_;
            pc_ = uint16_t(pc_ + (wide ? 2 : 1));
            bank0_mode_ = true;  // operand bytes wrap inside the program bank
            return address;
        }
        case Mode::Dp: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            bank0_mode_ = true;
            return dp_addr(off);
        }
        case Mode::DpX: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            bank0_mode_ = true;
            return dp_addr(uint32_t(off) + x);
        }
        case Mode::DpY: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            bank0_mode_ = true;
            return dp_addr(uint32_t(off) + y);
        }
        case Mode::DpInd: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            uint16_t ptr = uint16_t(rd(dp_addr(off)) | (rd(dp_addr(uint32_t(off) + 1)) << 8));
            return (uint32_t(dbr) << 16) | ptr;
        }
        case Mode::DpIndX: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            uint32_t lo_addr = dp_addr(uint32_t(off) + x);
            uint16_t ptr = uint16_t(rd(lo_addr) | (rd(uint16_t(lo_addr + 1)) << 8));
            return (uint32_t(dbr) << 16) | ptr;
        }
        case Mode::DpIndY: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            uint16_t ptr = uint16_t(rd(dp_addr(off)) | (rd(dp_addr(uint32_t(off) + 1)) << 8));
            uint32_t base = (uint32_t(dbr) << 16) | ptr;
            uint32_t address = (base + y) & 0xffffff;
            if (is_write || !p.x || ((base ^ address) & 0xff00)) cycles_++;
            return address;
        }
        case Mode::DpIndLong:
        case Mode::DpIndLongY: {
            uint8_t off = fetch8();
            if (dl) cycles_++;
            uint16_t pa = uint16_t(d + off);
            uint32_t ptr = uint32_t(rd(pa)) | (uint32_t(rd(uint16_t(pa + 1))) << 8) |
                           (uint32_t(rd(uint16_t(pa + 2))) << 16);
            if (mode == Mode::DpIndLongY) ptr = (ptr + y) & 0xffffff;
            return ptr;
        }
        case Mode::Abs:
            return (uint32_t(dbr) << 16) | fetch16();
        case Mode::AbsX:
        case Mode::AbsY: {
            uint32_t base = (uint32_t(dbr) << 16) | fetch16();
            uint32_t address = (base + (mode == Mode::AbsX ? x : y)) & 0xffffff;
            if (is_write || !p.x || ((base ^ address) & 0xff00)) cycles_++;
            return address;
        }
        case Mode::Long:
            return fetch24();
        case Mode::LongX:
            return (fetch24() + x) & 0xffffff;
        case Mode::Sr: {
            uint8_t off = fetch8();
            bank0_mode_ = true;
            return uint16_t(sp + off);
        }
        case Mode::SrIndY: {
            uint8_t off = fetch8();
            uint16_t pa = uint16_t(sp + off);
            uint16_t ptr = uint16_t(rd(pa) | (rd(uint16_t(pa + 1)) << 8));
            return ((uint32_t(dbr) << 16) + ptr + y) & 0xffffff;
        }
    }
    return 0;
}

uint16_t W65C816::load(uint32_t address, bool wide) {
    if (!wide) return rd(address);
    if (bank0_mode_) {
        return uint16_t(rd(address) | (rd((address & 0xff0000) | uint16_t(address + 1)) << 8));
    }
    return rd16_long(address);
}

void W65C816::store(uint32_t address, uint16_t value, bool wide) {
    wr(address, uint8_t(value));
    if (!wide) return;
    if (bank0_mode_) {
        wr((address & 0xff0000) | uint16_t(address + 1), uint8_t(value >> 8));
    } else {
        wr(address + 1, uint8_t(value >> 8));
    }
}

// ---------------------------------------------------------------------------
// ALU
// ---------------------------------------------------------------------------

void W65C816::op_adc(uint16_t data) {
    int c = p.c ? 1 : 0;
    if (p.m) {
        int al = a & 0xff;
        int result;
        if (!p.d) {
            result = al + data + c;
        } else {
            result = (al & 0x0f) + (data & 0x0f) + c;
            if (result > 0x09) result += 0x06;
            c = result > 0x0f;
            result = (al & 0xf0) + (data & 0xf0) + (c << 4) + (result & 0x0f);
        }
        p.v = (~(al ^ data) & (al ^ result) & 0x80) != 0;
        if (p.d && result > 0x9f) result += 0x60;
        p.c = result > 0xff;
        set_nz8(uint8_t(result));
        a = uint16_t((a & 0xff00) | uint8_t(result));
    } else {
        int aw = a;
        int result;
        if (!p.d) {
            result = aw + data + c;
        } else {
            result = (aw & 0x000f) + (data & 0x000f) + c;
            if (result > 0x0009) result += 0x0006;
            c = result > 0x000f;
            result = (aw & 0x00f0) + (data & 0x00f0) + (c << 4) + (result & 0x000f);
            if (result > 0x009f) result += 0x0060;
            c = result > 0x00ff;
            result = (aw & 0x0f00) + (data & 0x0f00) + (c << 8) + (result & 0x00ff);
            if (result > 0x09ff) result += 0x0600;
            c = result > 0x0fff;
            result = (aw & 0xf000) + (data & 0xf000) + (c << 12) + (result & 0x0fff);
        }
        p.v = (~(aw ^ data) & (aw ^ result) & 0x8000) != 0;
        if (p.d && result > 0x9fff) result += 0x6000;
        p.c = result > 0xffff;
        set_nz16(uint16_t(result));
        a = uint16_t(result);
    }
}

void W65C816::op_sbc(uint16_t data) {
    int c = p.c ? 1 : 0;
    if (p.m) {
        data = uint16_t(data ^ 0xff);
        int al = a & 0xff;
        int result;
        if (!p.d) {
            result = al + data + c;
        } else {
            result = (al & 0x0f) + (data & 0x0f) + c;
            if (result <= 0x0f) result -= 0x06;
            c = result > 0x0f;
            result = (al & 0xf0) + (data & 0xf0) + (c << 4) + (result & 0x0f);
        }
        p.v = (~(al ^ data) & (al ^ result) & 0x80) != 0;
        if (p.d && result <= 0xff) result -= 0x60;
        p.c = result > 0xff;
        set_nz8(uint8_t(result));
        a = uint16_t((a & 0xff00) | uint8_t(result));
    } else {
        data = uint16_t(data ^ 0xffff);
        int aw = a;
        int result;
        if (!p.d) {
            result = aw + data + c;
        } else {
            result = (aw & 0x000f) + (data & 0x000f) + c;
            if (result <= 0x000f) result -= 0x0006;
            c = result > 0x000f;
            result = (aw & 0x00f0) + (data & 0x00f0) + (c << 4) + (result & 0x000f);
            if (result <= 0x00ff) result -= 0x0060;
            c = result > 0x00ff;
            result = (aw & 0x0f00) + (data & 0x0f00) + (c << 8) + (result & 0x00ff);
            if (result <= 0x0fff) result -= 0x0600;
            c = result > 0x0fff;
            result = (aw & 0xf000) + (data & 0xf000) + (c << 12) + (result & 0x0fff);
        }
        p.v = (~(aw ^ data) & (aw ^ result) & 0x8000) != 0;
        if (p.d && result <= 0xffff) result -= 0x6000;
        p.c = result > 0xffff;
        set_nz16(uint16_t(result));
        a = uint16_t(result);
    }
}

void W65C816::op_cmp(uint16_t reg, uint16_t value, bool wide) {
    if (wide) {
        int t = int(reg) - int(value);
        p.c = t >= 0;
        set_nz16(uint16_t(t));
    } else {
        int t = int(reg & 0xff) - int(value & 0xff);
        p.c = t >= 0;
        set_nz8(uint8_t(t));
    }
}

void W65C816::branch(bool condition) {
    int8_t off = int8_t(fetch8());
    cycles_ += 2;
    if (condition) {
        uint16_t target = uint16_t(pc_ + off);
        cycles_ += 1;
        if (e_ && ((target ^ pc_) & 0xff00)) cycles_ += 1;
        pc_ = target;
    }
}

// op: 0=ASL 1=ROL 2=LSR 3=ROR 4=INC 5=DEC 6=TSB 7=TRB
void W65C816::rmw(uint32_t address, int op) {
    const bool wide = !p.m;
    uint16_t v = load(address, wide);
    const uint16_t msb = wide ? 0x8000 : 0x80;
    const uint16_t mask = wide ? 0xffff : 0xff;
    switch (op) {
        case 0: p.c = (v & msb) != 0; v = uint16_t((v << 1) & mask); break;
        case 1: { bool c = (v & msb) != 0; v = uint16_t(((v << 1) | (p.c ? 1 : 0)) & mask); p.c = c; break; }
        case 2: p.c = (v & 1) != 0; v = uint16_t(v >> 1); break;
        case 3: { bool c = (v & 1) != 0; v = uint16_t((v >> 1) | (p.c ? msb : 0)); p.c = c; break; }
        case 4: v = uint16_t((v + 1) & mask); break;
        case 5: v = uint16_t((v - 1) & mask); break;
        case 6: p.z = (v & a & mask) == 0; v = uint16_t((v | a) & mask); break;
        case 7: p.z = (v & a & mask) == 0; v = uint16_t(v & ~a & mask); break;
    }
    if (op < 6) {
        if (wide) set_nz16(v); else set_nz8(uint8_t(v));
    }
    if (wide) cycles_ += 2;
    store(address, v, wide);
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

int W65C816::run(int cycles) {
    int executed = 0;
    end_slice_ = false;
    while (executed < cycles && !end_slice_) {
        executed += step();
    }
    return executed;
}

int W65C816::step() {
    cycles_ = 0;

    bool nmi = false;
    if (nmi_request_ != IrqLine::Clear && !nmi_latched_) {
        nmi = true;
        nmi_latched_ = true;
        if (nmi_request_ == IrqLine::Pulse) {
            nmi_request_ = IrqLine::Clear;
            nmi_latched_ = false;
        }
    }
    const bool irq = irq_request_ != IrqLine::Clear;

    if (waiting_ && (irq || nmi)) waiting_ = false;

    if (nmi) {
        stopped_ = false;
        interrupt(0xffea, 0xfffa, false);
    } else if (irq && !p.i && !stopped_) {
        interrupt(0xffee, 0xfffe, false);
        if (irq_request_ == IrqLine::Pulse) irq_request_ = IrqLine::Clear;
    }
    if (cycles_ != 0) {
        if (cycle_handler_) cycle_handler_(cycles_);
        return cycles_;
    }

    if (stopped_ || waiting_) {
        cycles_ = 2;
        if (cycle_handler_) cycle_handler_(cycles_);
        return cycles_;
    }

    if (fetch_hook_) fetch_hook_(pc());
    const uint8_t op = fetch8();
    const bool m16 = !p.m;
    const bool x16 = !p.x;

    // --- Regular ALU group (ORA/AND/EOR/ADC/STA/LDA/CMP/SBC) ---
    if (((op & 0x01) && (op & 0x0f) != 0x0b) || (op & 0x1f) == 0x12) {
        if (op != 0x89) {
            Mode mode;
            int base;
            switch (op & 0x1f) {
                case 0x01: mode = Mode::DpIndX; base = 6; break;
                case 0x03: mode = Mode::Sr; base = 4; break;
                case 0x05: mode = Mode::Dp; base = 3; break;
                case 0x07: mode = Mode::DpIndLong; base = 6; break;
                case 0x09: mode = Mode::Imm; base = 2; break;
                case 0x0d: mode = Mode::Abs; base = 4; break;
                case 0x0f: mode = Mode::Long; base = 5; break;
                case 0x11: mode = Mode::DpIndY; base = 5; break;
                case 0x12: mode = Mode::DpInd; base = 5; break;
                case 0x13: mode = Mode::SrIndY; base = 7; break;
                case 0x15: mode = Mode::DpX; base = 4; break;
                case 0x17: mode = Mode::DpIndLongY; base = 6; break;
                case 0x19: mode = Mode::AbsY; base = 4; break;
                case 0x1d: mode = Mode::AbsX; base = 4; break;
                case 0x1f: mode = Mode::LongX; base = 5; break;
                default: mode = Mode::Abs; base = 4; break;
            }
            const int group = op >> 5;
            const bool is_store = group == 4;
            uint32_t address = ea(mode, m16, is_store);
            cycles_ += base + (m16 ? 1 : 0);
            switch (group) {
                case 0: { uint16_t v = load(address, m16); if (m16) { a |= v; set_nz16(a); } else { a = uint16_t(a | v); set_nz8(uint8_t(a)); } break; }
                case 1: { uint16_t v = load(address, m16); if (m16) { a &= v; set_nz16(a); } else { a = uint16_t(a & (0xff00 | v)); set_nz8(uint8_t(a)); } break; }
                case 2: { uint16_t v = load(address, m16); a = uint16_t(a ^ v); if (m16) set_nz16(a); else set_nz8(uint8_t(a)); break; }
                case 3: op_adc(load(address, m16)); break;
                case 4: store(address, a, m16); break;
                case 5: { uint16_t v = load(address, m16); if (m16) { a = v; set_nz16(a); } else { a = uint16_t((a & 0xff00) | v); set_nz8(uint8_t(v)); } break; }
                case 6: op_cmp(a, load(address, m16), m16); break;
                case 7: op_sbc(load(address, m16)); break;
            }
            if (cycle_handler_) cycle_handler_(cycles_);
            return cycles_;
        }
    }

    auto ldx = [&](Mode mode, uint16_t& reg, int base) {
        uint32_t address = ea(mode, x16, false);
        reg = load(address, x16);
        if (x16) set_nz16(reg); else set_nz8(uint8_t(reg));
        cycles_ += base + (x16 ? 1 : 0);
    };
    auto stx = [&](Mode mode, uint16_t reg, int base) {
        uint32_t address = ea(mode, x16, true);
        store(address, reg, x16);
        cycles_ += base + (x16 ? 1 : 0);
    };
    auto cpx = [&](Mode mode, uint16_t reg, int base) {
        uint32_t address = ea(mode, x16, false);
        op_cmp(reg, load(address, x16), x16);
        cycles_ += base + (x16 ? 1 : 0);
    };
    auto stz = [&](Mode mode, int base) {
        uint32_t address = ea(mode, m16, true);
        store(address, 0, m16);
        cycles_ += base + (m16 ? 1 : 0);
    };
    auto bit = [&](Mode mode, int base) {
        uint32_t address = ea(mode, m16, false);
        uint16_t v = load(address, m16);
        cycles_ += base + (m16 ? 1 : 0);
        if (m16) {
            p.z = (v & a) == 0;
            if (mode != Mode::Imm) { p.n = (v & 0x8000) != 0; p.v = (v & 0x4000) != 0; }
        } else {
            p.z = (v & a & 0xff) == 0;
            if (mode != Mode::Imm) { p.n = (v & 0x80) != 0; p.v = (v & 0x40) != 0; }
        }
    };
    auto rmw_op = [&](Mode mode, int which, int base) {
        uint32_t address = ea(mode, m16, true);
        cycles_ += base;
        rmw(address, which);
    };
    auto set_x_nz = [&](uint16_t v) { if (x16) set_nz16(v); else set_nz8(uint8_t(v)); };
    auto set_a_nz = [&]() { if (m16) set_nz16(a); else set_nz8(uint8_t(a)); };

    switch (op) {
        // --- Interrupts / control ---
        case 0x00: fetch8(); interrupt(0xffe6, 0xfffe, true); break;  // BRK
        case 0x02: fetch8(); interrupt(0xffe4, 0xfff4, true); break;  // COP
        case 0xdb: stopped_ = true; cycles_ = 3; break;                // STP
        case 0xcb: waiting_ = true; cycles_ = 3; break;                // WAI
        case 0xea: cycles_ = 2; break;                                 // NOP
        case 0x42: {                                                   // WDM
            uint8_t sig = fetch8();
            cycles_ = 2;
            if (wdm_handler_) wdm_handler_(sig);
            break;
        }

        case 0x18: p.c = false; cycles_ = 2; break;
        case 0x38: p.c = true; cycles_ = 2; break;
        case 0x58: p.i = false; cycles_ = 2; break;
        case 0x78: p.i = true; cycles_ = 2; break;
        case 0xb8: p.v = false; cycles_ = 2; break;
        case 0xd8: p.d = false; cycles_ = 2; break;
        case 0xf8: p.d = true; cycles_ = 2; break;

        case 0xfb: {  // XCE
            bool old_e = e_;
            set_emulation(p.c);
            p.c = old_e;
            cycles_ = 2;
            break;
        }
        case 0xc2: set_p(uint8_t(get_p() & ~fetch8())); cycles_ = 3; break;  // REP
        case 0xe2: set_p(uint8_t(get_p() | fetch8())); cycles_ = 3; break;   // SEP

        // --- Stack ---
        case 0x08: push8(get_p()); cycles_ = 3; break;          // PHP
        case 0x28: set_p(pull8()); cycles_ = 4; break;          // PLP
        case 0x48:                                              // PHA
            if (m16) push8(uint8_t(a >> 8));
            push8(uint8_t(a));
            cycles_ = m16 ? 4 : 3;
            break;
        case 0x68:                                              // PLA
            if (m16) { uint8_t lo = pull8(); a = uint16_t(lo | (pull8() << 8)); }
            else a = uint16_t((a & 0xff00) | pull8());
            set_a_nz();
            cycles_ = m16 ? 5 : 4;
            break;
        case 0xda:                                              // PHX
            if (x16) push8(uint8_t(x >> 8));
            push8(uint8_t(x));
            cycles_ = x16 ? 4 : 3;
            break;
        case 0xfa:                                              // PLX
            if (x16) { uint8_t lo = pull8(); x = uint16_t(lo | (pull8() << 8)); }
            else x = pull8();
            set_x_nz(x);
            cycles_ = x16 ? 5 : 4;
            break;
        case 0x5a:                                              // PHY
            if (x16) push8(uint8_t(y >> 8));
            push8(uint8_t(y));
            cycles_ = x16 ? 4 : 3;
            break;
        case 0x7a:                                              // PLY
            if (x16) { uint8_t lo = pull8(); y = uint16_t(lo | (pull8() << 8)); }
            else y = pull8();
            set_x_nz(y);
            cycles_ = x16 ? 5 : 4;
            break;
        case 0x8b: push8(dbr); cycles_ = 3; break;                              // PHB
        case 0x4b: push8(pbr); cycles_ = 3; break;                              // PHK
        case 0xab: dbr = pull8n(); fix_sp(); set_nz8(dbr); cycles_ = 4; break;  // PLB
        case 0x0b: push8n(uint8_t(d >> 8)); push8n(uint8_t(d)); fix_sp(); cycles_ = 4; break;  // PHD
        case 0x2b: {                                                                              // PLD
            uint8_t lo = pull8n();
            d = uint16_t(lo | (pull8n() << 8));
            fix_sp();
            set_nz16(d);
            cycles_ = 5;
            break;
        }
        case 0xf4: {  // PEA
            uint16_t v = fetch16();
            push8n(uint8_t(v >> 8));
            push8n(uint8_t(v));
            fix_sp();
            cycles_ = 5;
            break;
        }
        case 0xd4: {  // PEI
            uint8_t off = fetch8();
            if (d & 0xff) cycles_++;
            uint16_t v = rd16_bank0(uint16_t(d + off));
            push8n(uint8_t(v >> 8));
            push8n(uint8_t(v));
            fix_sp();
            cycles_ += 6;
            break;
        }
        case 0x62: {  // PER
            uint16_t off = fetch16();
            uint16_t v = uint16_t(pc_ + off);
            push8n(uint8_t(v >> 8));
            push8n(uint8_t(v));
            fix_sp();
            cycles_ = 6;
            break;
        }

        // --- Transfers ---
        case 0x1b: sp = e_ ? uint16_t(0x100 | (a & 0xff)) : a; cycles_ = 2; break;  // TCS
        case 0x3b: a = sp; set_nz16(a); cycles_ = 2; break;                          // TSC
        case 0x5b: d = a; set_nz16(d); cycles_ = 2; break;                           // TCD
        case 0x7b: a = d; set_nz16(a); cycles_ = 2; break;                           // TDC
        case 0xeb: a = uint16_t((a << 8) | (a >> 8)); set_nz8(uint8_t(a)); cycles_ = 3; break;  // XBA
        case 0xaa: x = x16 ? a : uint16_t(a & 0xff); set_x_nz(x); cycles_ = 2; break;  // TAX
        case 0xa8: y = x16 ? a : uint16_t(a & 0xff); set_x_nz(y); cycles_ = 2; break;  // TAY
        case 0x8a: a = m16 ? x : uint16_t((a & 0xff00) | (x & 0xff)); set_a_nz(); cycles_ = 2; break;  // TXA
        case 0x98: a = m16 ? y : uint16_t((a & 0xff00) | (y & 0xff)); set_a_nz(); cycles_ = 2; break;  // TYA
        case 0xba: x = x16 ? sp : uint16_t(sp & 0xff); set_x_nz(x); cycles_ = 2; break;  // TSX
        case 0x9a: sp = e_ ? uint16_t(0x100 | (x & 0xff)) : x; cycles_ = 2; break;       // TXS
        case 0x9b: y = x; set_x_nz(y); cycles_ = 2; break;                                // TXY
        case 0xbb: x = y; set_x_nz(x); cycles_ = 2; break;                                // TYX

        // --- Register inc/dec ---
        case 0xc8: y = x16 ? uint16_t(y + 1) : uint16_t((y + 1) & 0xff); set_x_nz(y); cycles_ = 2; break;
        case 0xe8: x = x16 ? uint16_t(x + 1) : uint16_t((x + 1) & 0xff); set_x_nz(x); cycles_ = 2; break;
        case 0x88: y = x16 ? uint16_t(y - 1) : uint16_t((y - 1) & 0xff); set_x_nz(y); cycles_ = 2; break;
        case 0xca: x = x16 ? uint16_t(x - 1) : uint16_t((x - 1) & 0xff); set_x_nz(x); cycles_ = 2; break;
        case 0x1a:
            a = m16 ? uint16_t(a + 1) : uint16_t((a & 0xff00) | uint8_t(a + 1));
            set_a_nz();
            cycles_ = 2;
            break;
        case 0x3a:
            a = m16 ? uint16_t(a - 1) : uint16_t((a & 0xff00) | uint8_t(a - 1));
            set_a_nz();
            cycles_ = 2;
            break;

        // --- Accumulator shifts ---
        case 0x0a:
            if (m16) { p.c = (a & 0x8000) != 0; a = uint16_t(a << 1); }
            else { p.c = (a & 0x80) != 0; a = uint16_t((a & 0xff00) | uint8_t(a << 1)); }
            set_a_nz(); cycles_ = 2; break;
        case 0x4a:
            if (m16) { p.c = (a & 1) != 0; a = uint16_t(a >> 1); }
            else { p.c = (a & 1) != 0; a = uint16_t((a & 0xff00) | ((a & 0xff) >> 1)); }
            set_a_nz(); cycles_ = 2; break;
        case 0x2a: {
            bool c = p.c;
            if (m16) { p.c = (a & 0x8000) != 0; a = uint16_t((a << 1) | (c ? 1 : 0)); }
            else { p.c = (a & 0x80) != 0; a = uint16_t((a & 0xff00) | uint8_t((a << 1) | (c ? 1 : 0))); }
            set_a_nz(); cycles_ = 2; break;
        }
        case 0x6a: {
            bool c = p.c;
            if (m16) { p.c = (a & 1) != 0; a = uint16_t((a >> 1) | (c ? 0x8000 : 0)); }
            else { p.c = (a & 1) != 0; a = uint16_t((a & 0xff00) | (((a & 0xff) >> 1) | (c ? 0x80 : 0))); }
            set_a_nz(); cycles_ = 2; break;
        }

        // --- Branches ---
        case 0x10: branch(!p.n); break;
        case 0x30: branch(p.n); break;
        case 0x50: branch(!p.v); break;
        case 0x70: branch(p.v); break;
        case 0x90: branch(!p.c); break;
        case 0xb0: branch(p.c); break;
        case 0xd0: branch(!p.z); break;
        case 0xf0: branch(p.z); break;
        case 0x80: branch(true); break;  // BRA
        case 0x82: {                     // BRL
            uint16_t off = fetch16();
            pc_ = uint16_t(pc_ + off);
            cycles_ = 4;
            break;
        }

        // --- Jumps / calls ---
        case 0x4c: pc_ = fetch16(); cycles_ = 3; break;  // JMP abs
        case 0x6c: {                                     // JMP (abs)
            uint16_t ptr = fetch16();
            pc_ = rd16_bank0(ptr);
            cycles_ = 5;
            break;
        }
        case 0x7c: {  // JMP (abs,X)
            uint16_t ptr = uint16_t(fetch16() + x);
            pc_ = uint16_t(rd((uint32_t(pbr) << 16) | ptr) | (rd((uint32_t(pbr) << 16) | uint16_t(ptr + 1)) << 8));
            cycles_ = 6;
            break;
        }
        case 0xdc: {  // JML [abs]
            uint16_t ptr = fetch16();
            uint16_t target = rd16_bank0(ptr);
            pbr = rd(uint16_t(ptr + 2));
            pc_ = target;
            cycles_ = 6;
            break;
        }
        case 0x5c: {  // JML long
            uint32_t target = fetch24();
            pc_ = uint16_t(target);
            pbr = uint8_t(target >> 16);
            cycles_ = 4;
            break;
        }
        case 0x20: {  // JSR abs
            uint16_t target = fetch16();
            uint16_t ret = uint16_t(pc_ - 1);
            push8(uint8_t(ret >> 8));
            push8(uint8_t(ret));
            pc_ = target;
            cycles_ = 6;
            break;
        }
        case 0xfc: {  // JSR (abs,X)
            uint8_t lo = fetch8();
            uint16_t ret = pc_;  // points at the operand's high byte
            push8(uint8_t(ret >> 8));
            push8(uint8_t(ret));
            uint8_t hi = fetch8();
            uint16_t ptr = uint16_t((lo | (hi << 8)) + x);
            pc_ = uint16_t(rd((uint32_t(pbr) << 16) | ptr) | (rd((uint32_t(pbr) << 16) | uint16_t(ptr + 1)) << 8));
            fix_sp();
            cycles_ = 8;
            break;
        }
        case 0x22: {  // JSL long
            uint16_t lo = fetch16();
            push8n(pbr);
            uint8_t bank = fetch8();
            uint16_t ret = uint16_t(pc_ - 1);
            push8n(uint8_t(ret >> 8));
            push8n(uint8_t(ret));
            fix_sp();
            pbr = bank;
            pc_ = lo;
            cycles_ = 8;
            break;
        }
        case 0x60: {  // RTS
            uint8_t lo = pull8();
            pc_ = uint16_t((lo | (pull8() << 8)) + 1);
            cycles_ = 6;
            break;
        }
        case 0x6b: {  // RTL
            uint8_t lo = pull8n();
            uint8_t hi = pull8n();
            pbr = pull8n();
            fix_sp();
            pc_ = uint16_t((lo | (hi << 8)) + 1);
            cycles_ = 6;
            break;
        }
        case 0x40: {  // RTI
            set_p(pull8());
            uint8_t lo = pull8();
            pc_ = uint16_t(lo | (pull8() << 8));
            if (!e_) pbr = pull8();
            cycles_ = e_ ? 6 : 7;
            break;
        }

        // --- Block moves ---
        case 0x54:
        case 0x44: {
            uint8_t dst_bank = fetch8(), src_bank = fetch8();
            dbr = dst_bank;
            wr((uint32_t(dst_bank) << 16) | y, rd((uint32_t(src_bank) << 16) | x));
            int delta = op == 0x54 ? 1 : -1;
            if (x16) {
                x = uint16_t(x + delta);
                y = uint16_t(y + delta);
            } else {
                x = uint16_t((x + delta) & 0xff);
                y = uint16_t((y + delta) & 0xff);
            }
            a = uint16_t(a - 1);
            if (a != 0xffff) pc_ = uint16_t(pc_ - 3);
            cycles_ = 7;
            break;
        }

        // --- BIT / TSB / TRB / STZ ---
        case 0x24: bit(Mode::Dp, 3); break;
        case 0x2c: bit(Mode::Abs, 4); break;
        case 0x34: bit(Mode::DpX, 4); break;
        case 0x3c: bit(Mode::AbsX, 4); break;
        case 0x89: bit(Mode::Imm, 2); break;
        case 0x04: rmw_op(Mode::Dp, 6, 5); break;                       // TSB dp
        case 0x0c: rmw_op(Mode::Abs, 6, 6); break;                      // TSB abs
        case 0x14: rmw_op(Mode::Dp, 7, 5); break;                       // TRB dp
        case 0x1c: rmw_op(Mode::Abs, 7, 6); break;                      // TRB abs
        case 0x64: stz(Mode::Dp, 3); break;
        case 0x74: stz(Mode::DpX, 4); break;
        case 0x9c: stz(Mode::Abs, 4); break;
        case 0x9e: stz(Mode::AbsX, 4); break;

        // --- Memory shifts / inc / dec ---
        case 0x06: rmw_op(Mode::Dp, 0, 5); break;
        case 0x0e: rmw_op(Mode::Abs, 0, 6); break;
        case 0x16: rmw_op(Mode::DpX, 0, 6); break;
        case 0x1e: rmw_op(Mode::AbsX, 0, 6); break;
        case 0x26: rmw_op(Mode::Dp, 1, 5); break;
        case 0x2e: rmw_op(Mode::Abs, 1, 6); break;
        case 0x36: rmw_op(Mode::DpX, 1, 6); break;
        case 0x3e: rmw_op(Mode::AbsX, 1, 6); break;
        case 0x46: rmw_op(Mode::Dp, 2, 5); break;
        case 0x4e: rmw_op(Mode::Abs, 2, 6); break;
        case 0x56: rmw_op(Mode::DpX, 2, 6); break;
        case 0x5e: rmw_op(Mode::AbsX, 2, 6); break;
        case 0x66: rmw_op(Mode::Dp, 3, 5); break;
        case 0x6e: rmw_op(Mode::Abs, 3, 6); break;
        case 0x76: rmw_op(Mode::DpX, 3, 6); break;
        case 0x7e: rmw_op(Mode::AbsX, 3, 6); break;
        case 0xe6: rmw_op(Mode::Dp, 4, 5); break;
        case 0xee: rmw_op(Mode::Abs, 4, 6); break;
        case 0xf6: rmw_op(Mode::DpX, 4, 6); break;
        case 0xfe: rmw_op(Mode::AbsX, 4, 6); break;
        case 0xc6: rmw_op(Mode::Dp, 5, 5); break;
        case 0xce: rmw_op(Mode::Abs, 5, 6); break;
        case 0xd6: rmw_op(Mode::DpX, 5, 6); break;
        case 0xde: rmw_op(Mode::AbsX, 5, 6); break;

        // --- X/Y loads, stores, compares ---
        case 0xa2: ldx(Mode::Imm, x, 2); break;
        case 0xa6: ldx(Mode::Dp, x, 3); break;
        case 0xae: ldx(Mode::Abs, x, 4); break;
        case 0xb6: ldx(Mode::DpY, x, 4); break;
        case 0xbe: ldx(Mode::AbsY, x, 4); break;
        case 0xa0: ldx(Mode::Imm, y, 2); break;
        case 0xa4: ldx(Mode::Dp, y, 3); break;
        case 0xac: ldx(Mode::Abs, y, 4); break;
        case 0xb4: ldx(Mode::DpX, y, 4); break;
        case 0xbc: ldx(Mode::AbsX, y, 4); break;
        case 0x86: stx(Mode::Dp, x, 3); break;
        case 0x8e: stx(Mode::Abs, x, 4); break;
        case 0x96: stx(Mode::DpY, x, 4); break;
        case 0x84: stx(Mode::Dp, y, 3); break;
        case 0x8c: stx(Mode::Abs, y, 4); break;
        case 0x94: stx(Mode::DpX, y, 4); break;
        case 0xc0: cpx(Mode::Imm, y, 2); break;
        case 0xc4: cpx(Mode::Dp, y, 3); break;
        case 0xcc: cpx(Mode::Abs, y, 4); break;
        case 0xe0: cpx(Mode::Imm, x, 2); break;
        case 0xe4: cpx(Mode::Dp, x, 3); break;
        case 0xec: cpx(Mode::Abs, x, 4); break;

        default:
            cycles_ = 2;
            break;
    }

    if (cycle_handler_) cycle_handler_(cycles_);
    return cycles_;
}

}  // namespace dsp
