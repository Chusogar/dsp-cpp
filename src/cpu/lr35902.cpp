#include "cpu/lr35902.h"

namespace dsp {

// gb_t: base T-state cost of every main-table opcode.
const uint8_t LR35902::kCycles[256] = {
     4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,  // 0
     4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,  // 1
     8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,  // 2
     8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,  // 3
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 4
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 5
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 6
     8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,  // 7
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 8
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 9
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // a
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // b
     8,12,12,16,12,16, 8,16, 8,16,12, 0,12,24, 8,16,  // c
     8,12,12, 4,12,16, 8,16, 8,16,12, 4,12, 4, 8,16,  // d
    12,12, 8, 4, 4,16, 8,16,16, 4,16, 4, 4, 4, 8,16,  // e
    12,12, 8, 4, 4,16, 8,16,12, 8,16, 4, 4, 4, 8,16,  // f
};

const uint8_t LR35902::kCyclesCb[256] = {
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 0
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 1
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 2
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 3
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 4
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 5
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 6
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 7
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 8
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 9
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // a
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // b
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // c
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // d
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // e
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // f
};

LR35902::LR35902(uint32_t clock) : clock_(clock) {}

void LR35902::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void LR35902::reset() {
    speed = 0;
    change_speed = false;
    changed_speed = false;
    sp = 0;
    pc = 0;
    a = b = c = d = e = h = l = 0;
    fz = fn = fh = fc = false;
    ime = false;
    after_ei_ = false;
    halt_ = false;
    vblank_ena = lcdstat_ena = timer_ena = serial_ena = joystick_ena = false;
    vblank_req = lcdstat_req = timer_req = serial_req = joystick_req = false;
}

uint8_t LR35902::fetch8() { return rd(pc++); }
uint16_t LR35902::fetch16() {
    uint16_t v = uint16_t(rd(pc) | (rd(uint16_t(pc + 1)) << 8));
    pc = uint16_t(pc + 2);
    return v;
}
void LR35902::push16(uint16_t value) {
    sp = uint16_t(sp - 2);
    wr(sp, uint8_t(value & 0xff));
    wr(uint16_t(sp + 1), uint8_t(value >> 8));
}
uint16_t LR35902::pop16() {
    uint16_t v = uint16_t(rd(sp) | (rd(uint16_t(sp + 1)) << 8));
    sp = uint16_t(sp + 2);
    return v;
}

uint8_t LR35902::get_f() const {
    return uint8_t((fz ? 0x80 : 0) | (fn ? 0x40 : 0) | (fh ? 0x20 : 0) | (fc ? 0x10 : 0));
}
void LR35902::set_f(uint8_t value) {
    fz = (value & 0x80) != 0;
    fn = (value & 0x40) != 0;
    fh = (value & 0x20) != 0;
    fc = (value & 0x10) != 0;
}

uint8_t LR35902::inc8(uint8_t v) {
    uint8_t r = uint8_t(v + 1);
    fz = r == 0;
    fn = false;
    fh = (v & 0x0f) == 0x0f;
    return r;
}
uint8_t LR35902::dec8(uint8_t v) {
    uint8_t r = uint8_t(v - 1);
    fz = r == 0;
    fn = true;
    fh = (v & 0x0f) == 0;
    return r;
}
void LR35902::add_a(uint8_t v) {
    int sum = a + v;
    fh = ((a & 0xf) + (v & 0xf)) > 0xf;
    fc = sum > 0xff;
    a = uint8_t(sum);
    fz = a == 0;
    fn = false;
}
void LR35902::adc_a(uint8_t v) {
    int carry = fc ? 1 : 0;
    int sum = a + v + carry;
    fh = ((a & 0xf) + (v & 0xf) + carry) > 0xf;
    fc = sum > 0xff;
    a = uint8_t(sum);
    fz = a == 0;
    fn = false;
}
void LR35902::sub_a(uint8_t v) {
    fh = (a & 0xf) < (v & 0xf);
    fc = a < v;
    a = uint8_t(a - v);
    fz = a == 0;
    fn = true;
}
void LR35902::sbc_a(uint8_t v) {
    int carry = fc ? 1 : 0;
    int diff = a - v - carry;
    fh = ((a & 0xf) - (v & 0xf) - carry) < 0;
    fc = diff < 0;
    a = uint8_t(diff);
    fz = a == 0;
    fn = true;
}
void LR35902::and_a(uint8_t v) {
    a = uint8_t(a & v);
    fz = a == 0;
    fn = false;
    fh = true;
    fc = false;
}
void LR35902::or_a(uint8_t v) {
    a = uint8_t(a | v);
    fz = a == 0;
    fn = false;
    fh = false;
    fc = false;
}
void LR35902::xor_a(uint8_t v) {
    a = uint8_t(a ^ v);
    fz = a == 0;
    fn = false;
    fh = false;
    fc = false;
}
void LR35902::cp_a(uint8_t v) {
    fh = (a & 0xf) < (v & 0xf);
    fc = a < v;
    uint8_t r = uint8_t(a - v);
    fz = r == 0;
    fn = true;
}
void LR35902::add_hl(uint16_t v) {
    uint16_t hl = uint16_t((h << 8) | l);
    uint32_t sum = uint32_t(hl) + v;
    fh = ((hl & 0xfff) + (v & 0xfff)) > 0xfff;
    fc = sum > 0xffff;
    hl = uint16_t(sum);
    h = uint8_t(hl >> 8);
    l = uint8_t(hl & 0xff);
    fn = false;
}
uint8_t LR35902::rlc(uint8_t v) {
    fc = (v & 0x80) != 0;
    v = uint8_t((v << 1) | (v >> 7));
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::rrc(uint8_t v) {
    fc = (v & 1) != 0;
    v = uint8_t((v >> 1) | (v << 7));
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::rl(uint8_t v) {
    uint8_t carry_in = fc ? 1 : 0;
    fc = (v & 0x80) != 0;
    v = uint8_t((v << 1) | carry_in);
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::rr(uint8_t v) {
    uint8_t carry_in = fc ? 0x80 : 0;
    fc = (v & 1) != 0;
    v = uint8_t((v >> 1) | carry_in);
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::sla(uint8_t v) {
    fc = (v & 0x80) != 0;
    v = uint8_t(v << 1);
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::sra(uint8_t v) {
    fc = (v & 1) != 0;
    v = uint8_t((v >> 1) | (v & 0x80));
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::srl(uint8_t v) {
    fc = (v & 1) != 0;
    v = uint8_t(v >> 1);
    fz = v == 0;
    fn = false;
    fh = false;
    return v;
}
uint8_t LR35902::swap(uint8_t v) {
    v = uint8_t((v << 4) | (v >> 4));
    fz = v == 0;
    fn = false;
    fh = false;
    fc = false;
    return v;
}
void LR35902::bit(uint8_t n, uint8_t v) {
    fz = ((v >> n) & 1) == 0;
    fn = false;
    fh = true;
}

int LR35902::service_interrupt() {
    struct Src {
        bool ena;
        bool& req;
        uint16_t vector;
    };
    Src sources[5] = {
        {vblank_ena, vblank_req, 0x40},   {lcdstat_ena, lcdstat_req, 0x48},
        {timer_ena, timer_req, 0x50},     {serial_ena, serial_req, 0x58},
        {joystick_ena, joystick_req, 0x60},
    };
    for (Src& src : sources) {
        if (src.ena && src.req) {
            int extra = 0;
            if (halt_) extra = 4;
            halt_ = false;
            if (ime) {
                ime = false;
                src.req = false;
                push16(pc);
                pc = src.vector;
                extra += 20;
                interrupts_serviced++;
            }
            return extra;
        }
    }
    return 0;
}

int LR35902::run(int cycles) {
    int total = 0;
    while (total < cycles) {
        bool check_interrupts = !after_ei_;
        after_ei_ = false;
        int extra = check_interrupts ? service_interrupt() : 0;

        if (halt_) {
            int c = 4 + extra;
            if (cycle_handler_) cycle_handler_(c);
            total += c;
            continue;
        }

        uint8_t opcode = fetch8();
        if (on_fetch) on_fetch(uint16_t(pc - 1));
        extra_cycles_ = 0;
        exec(opcode);
        int c = kCycles[opcode] + extra_cycles_ + extra;
        if (cycle_handler_) cycle_handler_(c);
        total += c;
    }
    return total;
}

void LR35902::exec_cb() {
    uint8_t op = fetch8();
    uint8_t reg = op & 7;
    uint8_t kind = op >> 3;
    auto get = [&]() -> uint8_t {
        switch (reg) {
            case 0: return b;
            case 1: return c;
            case 2: return d;
            case 3: return e;
            case 4: return h;
            case 5: return l;
            case 6: return rd(uint16_t((h << 8) | l));
            default: return a;
        }
    };
    auto set = [&](uint8_t v) {
        switch (reg) {
            case 0: b = v; break;
            case 1: c = v; break;
            case 2: d = v; break;
            case 3: e = v; break;
            case 4: h = v; break;
            case 5: l = v; break;
            case 6: wr(uint16_t((h << 8) | l), v); break;
            default: a = v; break;
        }
    };
    uint8_t v = get();
    if (kind < 8) {
        switch (kind) {
            case 0: set(rlc(v)); break;
            case 1: set(rrc(v)); break;
            case 2: set(rl(v)); break;
            case 3: set(rr(v)); break;
            case 4: set(sla(v)); break;
            case 5: set(sra(v)); break;
            case 6: set(swap(v)); break;
            default: set(srl(v)); break;
        }
    } else if (kind < 16) {
        bit(uint8_t(kind - 8), v);
    } else if (kind < 24) {
        set(uint8_t(v & ~(1 << (kind - 16))));  // RES
    } else {
        set(uint8_t(v | (1 << (kind - 24))));  // SET
    }
    extra_cycles_ = int(kCyclesCb[op]);  // kCycles[0xCB] is 0, this is the full cost
}

// 8-bit register access for the ALU/LD blocks that repeat every register
// (opcodes $40-$bf and the ALU-immediate/(HL)/reg forms use the same B,C,D,
// E,H,L,(HL),A ordering as the CB table above).
uint8_t LR35902::reg8(uint8_t index) const {
    switch (index) {
        case 0: return b;
        case 1: return c;
        case 2: return d;
        case 3: return e;
        case 4: return h;
        case 5: return l;
        case 6: return rd(uint16_t((h << 8) | l));
        default: return a;
    }
}
void LR35902::set_reg8(uint8_t index, uint8_t value) {
    switch (index) {
        case 0: b = value; break;
        case 1: c = value; break;
        case 2: d = value; break;
        case 3: e = value; break;
        case 4: h = value; break;
        case 5: l = value; break;
        case 6: wr(uint16_t((h << 8) | l), value); break;
        default: a = value; break;
    }
}

void LR35902::exec(uint8_t op) {
    uint16_t hl = uint16_t((h << 8) | l);
    switch (op) {
        // 0x00-0x0F
        case 0x00: break;  // NOP
        case 0x01: { uint16_t v = fetch16(); b = uint8_t(v >> 8); c = uint8_t(v); break; }
        case 0x02: wr(uint16_t((b << 8) | c), a); break;
        case 0x03: { uint16_t v = uint16_t(((b << 8) | c) + 1); b = uint8_t(v >> 8); c = uint8_t(v); break; }
        case 0x04: b = inc8(b); break;
        case 0x05: b = dec8(b); break;
        case 0x06: b = fetch8(); break;
        case 0x07: fc = (a & 0x80) != 0; a = uint8_t((a << 1) | (a >> 7)); fz = fn = fh = false; break;  // RLCA
        case 0x08: { uint16_t addr = fetch16(); wr(addr, uint8_t(sp & 0xff)); wr(uint16_t(addr + 1), uint8_t(sp >> 8)); break; }
        case 0x09: add_hl(uint16_t((b << 8) | c)); break;
        case 0x0a: a = rd(uint16_t((b << 8) | c)); break;
        case 0x0b: { uint16_t v = uint16_t(((b << 8) | c) - 1); b = uint8_t(v >> 8); c = uint8_t(v); break; }
        case 0x0c: c = inc8(c); break;
        case 0x0d: c = dec8(c); break;
        case 0x0e: c = fetch8(); break;
        case 0x0f: fc = (a & 1) != 0; a = uint8_t((a >> 1) | (a << 7)); fz = fn = fh = false; break;  // RRCA

        // 0x10-0x1F
        case 0x10:  // STOP
            fetch8();
            if (change_speed) { speed ^= 1; change_speed = false; changed_speed = true; }
            break;
        case 0x11: { uint16_t v = fetch16(); d = uint8_t(v >> 8); e = uint8_t(v); break; }
        case 0x12: wr(uint16_t((d << 8) | e), a); break;
        case 0x13: { uint16_t v = uint16_t(((d << 8) | e) + 1); d = uint8_t(v >> 8); e = uint8_t(v); break; }
        case 0x14: d = inc8(d); break;
        case 0x15: d = dec8(d); break;
        case 0x16: d = fetch8(); break;
        case 0x17: { bool carry_in = fc; fc = (a & 0x80) != 0; a = uint8_t((a << 1) | (carry_in ? 1 : 0)); fz = fn = fh = false; break; }  // RLA
        case 0x18: pc = uint16_t(pc + 1 + int8_t(fetch8())); break;  // JR n
        case 0x19: add_hl(uint16_t((d << 8) | e)); break;
        case 0x1a: a = rd(uint16_t((d << 8) | e)); break;
        case 0x1b: { uint16_t v = uint16_t(((d << 8) | e) - 1); d = uint8_t(v >> 8); e = uint8_t(v); break; }
        case 0x1c: e = inc8(e); break;
        case 0x1d: e = dec8(e); break;
        case 0x1e: e = fetch8(); break;
        case 0x1f: { bool carry_in = fc; fc = (a & 1) != 0; a = uint8_t((a >> 1) | (carry_in ? 0x80 : 0)); fz = fn = fh = false; break; }  // RRA

        // 0x20-0x2F
        case 0x20: { int8_t rel = int8_t(fetch8()); if (!fz) { pc = uint16_t(pc + rel); extra_cycles_ += 4; } break; }
        case 0x21: { uint16_t v = fetch16(); h = uint8_t(v >> 8); l = uint8_t(v); break; }
        case 0x22: wr(hl, a); hl++; h = uint8_t(hl >> 8); l = uint8_t(hl); break;
        case 0x23: { uint16_t v = uint16_t(hl + 1); h = uint8_t(v >> 8); l = uint8_t(v); break; }
        case 0x24: h = inc8(h); break;
        case 0x25: h = dec8(h); break;
        case 0x26: h = fetch8(); break;
        case 0x27: {  // DAA
            int t = a;
            if (!fn) {
                if (fh || (t & 0xf) > 9) t += 0x6;
                if (fc || t > 0x9f) t += 0x60;
            } else {
                if (fh) { t -= 6; if (!fc) t &= 0xff; }
                if (fc) t -= 0x60;
            }
            fh = false;
            if (t & 0x100) fc = true;
            a = uint8_t(t & 0xff);
            fz = a == 0;
            break;
        }
        case 0x28: { int8_t rel = int8_t(fetch8()); if (fz) { pc = uint16_t(pc + rel); extra_cycles_ += 4; } break; }
        case 0x29: add_hl(hl); break;
        case 0x2a: a = rd(hl); hl++; h = uint8_t(hl >> 8); l = uint8_t(hl); break;
        case 0x2b: { uint16_t v = uint16_t(hl - 1); h = uint8_t(v >> 8); l = uint8_t(v); break; }
        case 0x2c: l = inc8(l); break;
        case 0x2d: l = dec8(l); break;
        case 0x2e: l = fetch8(); break;
        case 0x2f: a = uint8_t(~a); fn = fh = true; break;  // CPL

        // 0x30-0x3F
        case 0x30: { int8_t rel = int8_t(fetch8()); if (!fc) { pc = uint16_t(pc + rel); extra_cycles_ += 4; } break; }
        case 0x31: sp = fetch16(); break;
        case 0x32: wr(hl, a); hl--; h = uint8_t(hl >> 8); l = uint8_t(hl); break;
        case 0x33: sp++; break;
        case 0x34: wr(hl, inc8(rd(hl))); break;
        case 0x35: wr(hl, dec8(rd(hl))); break;
        case 0x36: wr(hl, fetch8()); break;
        case 0x37: fc = true; fn = fh = false; break;  // SCF
        case 0x38: { int8_t rel = int8_t(fetch8()); if (fc) { pc = uint16_t(pc + rel); extra_cycles_ += 4; } break; }
        case 0x39: add_hl(sp); break;
        case 0x3a: a = rd(hl); hl--; h = uint8_t(hl >> 8); l = uint8_t(hl); break;
        case 0x3b: sp--; break;
        case 0x3c: a = inc8(a); break;
        case 0x3d: a = dec8(a); break;
        case 0x3e: a = fetch8(); break;
        case 0x3f: fc = !fc; fn = fh = false; break;  // CCF

        // 0x40-0x7F: LD r,r' and HALT
        case 0x76: halt_ = true; break;

        // 0x80-0xBF: ALU A,r
        case 0xc0: if (!fz) { pc = pop16(); extra_cycles_ += 12; } break;
        case 0xc1: { uint16_t v = pop16(); b = uint8_t(v >> 8); c = uint8_t(v); break; }
        case 0xc2: { uint16_t addr = fetch16(); if (!fz) { pc = addr; extra_cycles_ += 4; } break; }
        case 0xc3: pc = fetch16(); break;
        case 0xc4: { uint16_t addr = fetch16(); if (!fz) { push16(pc); pc = addr; extra_cycles_ += 12; } break; }
        case 0xc5: push16(uint16_t((b << 8) | c)); break;
        case 0xc6: add_a(fetch8()); break;
        case 0xc7: push16(pc); pc = 0x00; break;
        case 0xc8: if (fz) { pc = pop16(); extra_cycles_ += 12; } break;
        case 0xc9: pc = pop16(); break;
        case 0xca: { uint16_t addr = fetch16(); if (fz) { pc = addr; extra_cycles_ += 4; } break; }
        case 0xcb: exec_cb(); break;
        case 0xcc: { uint16_t addr = fetch16(); if (fz) { push16(pc); pc = addr; extra_cycles_ += 12; } break; }
        case 0xcd: { uint16_t addr = fetch16(); push16(pc); pc = addr; break; }
        case 0xce: adc_a(fetch8()); break;
        case 0xcf: push16(pc); pc = 0x08; break;

        case 0xd0: if (!fc) { pc = pop16(); extra_cycles_ += 12; } break;
        case 0xd1: { uint16_t v = pop16(); d = uint8_t(v >> 8); e = uint8_t(v); break; }
        case 0xd2: { uint16_t addr = fetch16(); if (!fc) { pc = addr; extra_cycles_ += 4; } break; }
        case 0xd4: { uint16_t addr = fetch16(); if (!fc) { push16(pc); pc = addr; extra_cycles_ += 12; } break; }
        case 0xd5: push16(uint16_t((d << 8) | e)); break;
        case 0xd6: sub_a(fetch8()); break;
        case 0xd7: push16(pc); pc = 0x10; break;
        case 0xd8: if (fc) { pc = pop16(); extra_cycles_ += 12; } break;
        case 0xd9: pc = pop16(); ime = true; after_ei_ = true; break;  // RETI (matches gb.pas: one-instruction delay, same as EI)
        case 0xda: { uint16_t addr = fetch16(); if (fc) { pc = addr; extra_cycles_ += 4; } break; }
        case 0xdc: { uint16_t addr = fetch16(); if (fc) { push16(pc); pc = addr; extra_cycles_ += 12; } break; }
        case 0xde: sbc_a(fetch8()); break;
        case 0xdf: push16(pc); pc = 0x18; break;

        case 0xe0: wr(uint16_t(0xff00 + fetch8()), a); break;  // LDH (n),A
        case 0xe1: { uint16_t v = pop16(); h = uint8_t(v >> 8); l = uint8_t(v); break; }
        case 0xe2: wr(uint16_t(0xff00 + c), a); break;  // LD (C),A
        case 0xe5: push16(hl); break;
        case 0xe6: and_a(fetch8()); break;
        case 0xe7: push16(pc); pc = 0x20; break;
        case 0xe8: {  // ADD SP,n
            int8_t rel = int8_t(fetch8());
            fz = fn = false;
            fc = ((sp & 0xff) + (uint8_t(rel) & 0xff)) > 0xff;
            fh = ((sp & 0xf) + (uint8_t(rel) & 0xf)) > 0xf;
            sp = uint16_t(sp + rel);
            break;
        }
        case 0xe9: pc = hl; break;  // JP (HL)
        case 0xea: wr(fetch16(), a); break;
        case 0xee: xor_a(fetch8()); break;
        case 0xef: push16(pc); pc = 0x28; break;

        case 0xf0: a = rd(uint16_t(0xff00 + fetch8())); break;  // LDH A,(n)
        case 0xf1: set_f(uint8_t(rd(sp) & 0xf0)); a = rd(uint16_t(sp + 1)); sp = uint16_t(sp + 2); break;  // POP AF
        case 0xf2: a = rd(uint16_t(0xff00 + c)); break;  // LD A,(C)
        case 0xf3: ime = false; after_ei_ = false; break;  // DI
        case 0xf5: push16(uint16_t((a << 8) | get_f())); break;  // PUSH AF
        case 0xf6: or_a(fetch8()); break;
        case 0xf7: push16(pc); pc = 0x30; break;
        case 0xf8: {  // LD HL,SP+n
            fz = fn = false;
            uint8_t rel = fetch8();
            fc = ((sp & 0xff) + rel) > 0xff;
            fh = ((sp & 0xf) + (rel & 0xf)) > 0xf;
            hl = uint16_t(sp + int8_t(rel));
            h = uint8_t(hl >> 8);
            l = uint8_t(hl);
            break;
        }
        case 0xf9: sp = hl; break;  // LD SP,HL
        case 0xfa: a = rd(fetch16()); break;
        case 0xfb: ime = true; after_ei_ = true; break;  // EI
        case 0xfe: cp_a(fetch8()); break;
        case 0xff: push16(pc); pc = 0x38; break;

        default:
            // 0x40-0x7f (LD r,r') and 0x80-0xbf (ALU A,r) share the same
            // "destination/operation in bits 3-5, source register in bits
            // 0-2" layout as the CB table, so they are decoded uniformly
            // here instead of as 128 separate cases.
            if (op >= 0x40 && op <= 0x7f) {
                set_reg8(uint8_t((op >> 3) & 7), reg8(uint8_t(op & 7)));
            } else if (op >= 0x80 && op <= 0xbf) {
                uint8_t v = reg8(uint8_t(op & 7));
                switch ((op >> 3) & 7) {
                    case 0: add_a(v); break;
                    case 1: adc_a(v); break;
                    case 2: sub_a(v); break;
                    case 3: sbc_a(v); break;
                    case 4: and_a(v); break;
                    case 5: xor_a(v); break;
                    case 6: or_a(v); break;
                    default: cp_a(v); break;
                }
            }
            break;
    }
}

}  // namespace dsp
