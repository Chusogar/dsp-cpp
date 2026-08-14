#include "cpu/lr35902.h"

#include <array>

namespace dsp {
namespace {

// Machine-cycle * 4 timing tables from lr35902.pas (gb_t / gb_cb_t).
// CB table values are the *extra* cycles beyond the base $CB fetch (4 T);
// the Pascal core adds them on top of gb_t[$CB]=0 effectively via estados_demas.
constexpr uint8_t kMain[256] = {
    // 0x00
    4, 12, 8, 8, 4, 4, 8, 4, 20, 8, 8, 8, 4, 4, 8, 4,
    // 0x10
    4, 12, 8, 8, 4, 4, 8, 4, 12, 8, 8, 8, 4, 4, 8, 4,
    // 0x20
    8, 12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
    // 0x30
    8, 12, 8, 8, 12, 12, 12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
    // 0x40
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0x50
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0x60
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0x70
    8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0x80
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0x90
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0xA0
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0xB0
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    // 0xC0
    8, 12, 12, 16, 12, 16, 8, 16, 8, 16, 12, 0, 12, 24, 8, 16,
    // 0xD0
    8, 12, 12, 4, 12, 16, 8, 16, 8, 16, 12, 4, 12, 4, 8, 16,
    // 0xE0
    12, 12, 8, 4, 4, 16, 8, 16, 16, 4, 16, 4, 4, 4, 8, 16,
    // 0xF0
    12, 12, 8, 4, 4, 16, 8, 16, 12, 8, 16, 4, 4, 4, 8, 16,
};

// Full CB instruction cost (already includes the CB prefix byte).
// Pascal stores only the extra part and adds it; here we use absolute totals
// matching the documented GB timings: 8 for reg ops, 12 for BIT (HL), 16 for
// RMW (HL).
constexpr uint8_t kCb[256] = {
    // RLC/RRC/RL/RR/SLA/SRA/SWAP/SRL  (8 reg, 16 (HL))
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0x00
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0x10
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0x20
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0x30
    // BIT (8 reg, 12 (HL))
    8, 8, 8, 8, 8, 8, 12, 8, 8, 8, 8, 8, 8, 8, 12, 8,  // 0x40
    8, 8, 8, 8, 8, 8, 12, 8, 8, 8, 8, 8, 8, 8, 12, 8,  // 0x50
    8, 8, 8, 8, 8, 8, 12, 8, 8, 8, 8, 8, 8, 8, 12, 8,  // 0x60
    8, 8, 8, 8, 8, 8, 12, 8, 8, 8, 8, 8, 8, 8, 12, 8,  // 0x70
    // RES / SET (8 reg, 16 (HL))
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0x80
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0x90
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0xA0
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0xB0
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0xC0
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0xD0
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0xE0
    8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8,  // 0xF0
};

}  // namespace

LR35902::LR35902(uint32_t clock) : clock_(clock) {}

void LR35902::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void LR35902::reset() {
    a = f = b = c = d = e = h = l = 0;
    sp = 0;
    pc_ = 0;
    ime_ = false;
    after_ei_ = false;
    halted_ = false;
    speed_ = 0;
    change_speed_ = false;
    changed_speed_ = false;
    vblank_ena_ = lcdstat_ena_ = timer_ena_ = serial_ena_ = joystick_ena_ = false;
    vblank_req_ = lcdstat_req_ = timer_req_ = serial_req_ = joystick_req_ = false;
    cycles_ = executed_ = 0;
}

uint8_t LR35902::fetch() {
    return rd(pc_++);
}

uint16_t LR35902::fetch16() {
    uint8_t lo = fetch();
    uint8_t hi = fetch();
    return uint16_t((hi << 8) | lo);
}

void LR35902::push(uint16_t value) {
    sp = uint16_t(sp - 1);
    wr(sp, uint8_t(value >> 8));
    sp = uint16_t(sp - 1);
    wr(sp, uint8_t(value));
}

uint16_t LR35902::pop() {
    uint8_t lo = rd(sp);
    sp = uint16_t(sp + 1);
    uint8_t hi = rd(sp);
    sp = uint16_t(sp + 1);
    return uint16_t((hi << 8) | lo);
}

void LR35902::charge(int tstates) {
    cycles_ += tstates;
    executed_ += tstates;
    if (cycle_handler_) cycle_handler_(tstates);
}

// ---- ALU -----------------------------------------------------------------

void LR35902::add_a(uint8_t value) {
    unsigned r1 = (a & 0x0f) + (value & 0x0f);
    unsigned r2 = a + value;
    a = uint8_t(r2);
    set_z(a == 0);
    set_n(false);
    set_h(r1 > 0x0f);
    set_c(r2 > 0xff);
}

void LR35902::adc_a(uint8_t value) {
    unsigned carry = flag_c() ? 1u : 0u;
    unsigned r1 = (a & 0x0f) + (value & 0x0f) + carry;
    unsigned r2 = a + value + carry;
    a = uint8_t(r2);
    set_z((r2 & 0xff) == 0);
    set_n(false);
    set_h(r1 > 0x0f);
    set_c(r2 > 0xff);
}

void LR35902::sub_a(uint8_t value) {
    unsigned r1 = (a & 0x0f) - (value & 0x0f);
    unsigned r2 = a - value;
    a = uint8_t(r2);
    set_z(a == 0);
    set_n(true);
    set_h(r1 > 0x0f);
    set_c(r2 > 0xff);
}

void LR35902::sbc_a(uint8_t value) {
    unsigned carry = flag_c() ? 1u : 0u;
    unsigned r1 = (a & 0x0f) - (value & 0x0f) - carry;
    unsigned r2 = a - value - carry;
    a = uint8_t(r2);
    set_z(a == 0);
    set_n(true);
    set_h(r1 > 0x0f);
    set_c(r2 > 0xff);
}

void LR35902::and_a(uint8_t value) {
    a &= value;
    set_z(a == 0);
    set_n(false);
    set_h(true);
    set_c(false);
}

void LR35902::xor_a(uint8_t value) {
    a ^= value;
    set_z(a == 0);
    set_n(false);
    set_h(false);
    set_c(false);
}

void LR35902::or_a(uint8_t value) {
    a |= value;
    set_z(a == 0);
    set_n(false);
    set_h(false);
    set_c(false);
}

void LR35902::cp_a(uint8_t value) {
    unsigned r1 = (a & 0x0f) - (value & 0x0f);
    unsigned r2 = a - value;
    set_z((r2 & 0xff) == 0);
    set_n(true);
    set_h(r1 > 0x0f);
    set_c(r2 > 0xff);
}

uint8_t LR35902::inc8(uint8_t value) {
    uint8_t res = uint8_t(value + 1);
    set_n(false);
    set_z(res == 0);
    set_h((res & 0x0f) == 0);
    return res;
}

uint8_t LR35902::dec8(uint8_t value) {
    uint8_t res = uint8_t(value - 1);
    set_n(true);
    set_z(res == 0);
    set_h((res & 0x0f) == 0x0f);
    return res;
}

void LR35902::add_hl(uint16_t value) {
    unsigned r1 = hl() + value;
    unsigned r2 = (hl() & 0x0fff) + (value & 0x0fff);
    set_n(false);
    set_c(r1 > 0xffff);
    set_h(r2 > 0x0fff);
    set_hl(uint16_t(r1));
}

// ---- rotates / shifts ----------------------------------------------------

uint8_t LR35902::rlc(uint8_t value) {
    uint8_t res = uint8_t((value << 1) | (value >> 7));
    set_c((res & 1) != 0);
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

uint8_t LR35902::rrc(uint8_t value) {
    uint8_t res = uint8_t((value >> 1) | (value << 7));
    set_c((res & 0x80) != 0);
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

uint8_t LR35902::rl(uint8_t value) {
    uint8_t carry_in = flag_c() ? 1 : 0;
    set_c((value & 0x80) != 0);
    uint8_t res = uint8_t((value << 1) | carry_in);
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

uint8_t LR35902::rr(uint8_t value) {
    uint8_t carry_in = flag_c() ? 0x80 : 0;
    set_c((value & 1) != 0);
    uint8_t res = uint8_t((value >> 1) | carry_in);
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

uint8_t LR35902::sla(uint8_t value) {
    set_c((value & 0x80) != 0);
    uint8_t res = uint8_t(value << 1);
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

uint8_t LR35902::sra(uint8_t value) {
    set_c((value & 1) != 0);
    uint8_t res = uint8_t((value >> 1) | (value & 0x80));
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

uint8_t LR35902::swap(uint8_t value) {
    uint8_t res = uint8_t((value >> 4) | (value << 4));
    set_z(res == 0);
    set_n(false);
    set_h(false);
    set_c(false);
    return res;
}

uint8_t LR35902::srl(uint8_t value) {
    set_c((value & 1) != 0);
    uint8_t res = uint8_t(value >> 1);
    set_z(res == 0);
    set_n(false);
    set_h(false);
    return res;
}

void LR35902::bit(uint8_t index, uint8_t value) {
    set_z((value & (1u << index)) == 0);
    set_n(false);
    set_h(true);
}

// ---- CB prefix -----------------------------------------------------------

void LR35902::exec_cb() {
    const uint8_t op = fetch();
    cycles_ = kCb[op];

    const uint8_t reg = op & 7;
    const uint8_t group = op >> 3;

    auto read_r = [&](uint8_t r) -> uint8_t {
        switch (r) {
            case 0: return b;
            case 1: return c;
            case 2: return d;
            case 3: return e;
            case 4: return h;
            case 5: return l;
            case 6: return rd(hl());
            default: return a;
        }
    };
    auto write_r = [&](uint8_t r, uint8_t v) {
        switch (r) {
            case 0: b = v; break;
            case 1: c = v; break;
            case 2: d = v; break;
            case 3: e = v; break;
            case 4: h = v; break;
            case 5: l = v; break;
            case 6: wr(hl(), v); break;
            default: a = v; break;
        }
    };

    if (group < 8) {
        // rotates / shifts / swap
        uint8_t v = read_r(reg);
        switch (group) {
            case 0: v = rlc(v); break;
            case 1: v = rrc(v); break;
            case 2: v = rl(v); break;
            case 3: v = rr(v); break;
            case 4: v = sla(v); break;
            case 5: v = sra(v); break;
            case 6: v = swap(v); break;
            case 7: v = srl(v); break;
        }
        write_r(reg, v);
    } else if (group < 16) {
        // BIT
        bit(uint8_t(group - 8), read_r(reg));
    } else if (group < 24) {
        // RES
        write_r(reg, uint8_t(read_r(reg) & ~(1u << (group - 16))));
    } else {
        // SET
        write_r(reg, uint8_t(read_r(reg) | (1u << (group - 24))));
    }
}

// ---- interrupts ----------------------------------------------------------

int LR35902::service_interrupts() {
    if (after_ei_) return 0;

    auto try_irq = [&](bool ena, bool& req, uint16_t vector) -> int {
        if (!(ena && req)) return 0;
        int extra = 0;
        if (halted_) extra = 4;  // +4 T when leaving HALT for an IRQ
        halted_ = false;
        if (ime_) {
            ime_ = false;
            req = false;
            push(pc_);
            pc_ = vector;
            extra += 20;
        }
        return extra;
    };

    if (int e = try_irq(vblank_ena_, vblank_req_, 0x40)) return e;
    if (int e = try_irq(lcdstat_ena_, lcdstat_req_, 0x48)) return e;
    if (int e = try_irq(timer_ena_, timer_req_, 0x50)) return e;
    if (int e = try_irq(serial_ena_, serial_req_, 0x58)) return e;
    if (int e = try_irq(joystick_ena_, joystick_req_, 0x60)) return e;
    return 0;
}

// ---- main loop -----------------------------------------------------------

int LR35902::run(int cycles) {
    executed_ = 0;
    while (executed_ < cycles) {
        cycles_ = 0;
        cycles_ += service_interrupts();
        after_ei_ = false;

        if (halted_) {
            // While halted the CPU still burns 4 T-states per "instruction"
            // and re-fetches the HALT opcode (PC was not advanced past it).
            // Pascal does: if halt then pc := pc - 1 before the fetch.
            // Equivalent: keep PC on the HALT byte and re-execute it as NOP.
            // We charge 4 T and leave PC alone (pointing at next byte after
            // the original HALT fetch). To match Pascal exactly:
            // Pascal decrements PC so the next fetch re-reads HALT ($76),
            // which is a no-op that costs 4 T and does not clear halt until
            // an interrupt arrives.
            pc_ = uint16_t(pc_ - 1);
        }

        const uint8_t op = fetch();
        cycles_ += kMain[op];

        switch (op) {
            case 0x00:  // NOP
                break;
            case 0x01:  // LD BC,nn
                set_bc(fetch16());
                break;
            case 0x02:  // LD (BC),A
                wr(bc(), a);
                break;
            case 0x03:  // INC BC
                set_bc(uint16_t(bc() + 1));
                break;
            case 0x04:  // INC B
                b = inc8(b);
                break;
            case 0x05:  // DEC B
                b = dec8(b);
                break;
            case 0x06:  // LD B,n
                b = fetch();
                break;
            case 0x07: {  // RLCA
                set_c((a & 0x80) != 0);
                a = uint8_t((a << 1) | (a >> 7));
                set_z(false);
                set_n(false);
                set_h(false);
                break;
            }
            case 0x08: {  // LD (nn),SP
                uint16_t addr = fetch16();
                wr(addr, uint8_t(sp));
                wr(uint16_t(addr + 1), uint8_t(sp >> 8));
                break;
            }
            case 0x09:  // ADD HL,BC
                add_hl(bc());
                break;
            case 0x0A:  // LD A,(BC)
                a = rd(bc());
                break;
            case 0x0B:  // DEC BC
                set_bc(uint16_t(bc() - 1));
                break;
            case 0x0C:  // INC C
                c = inc8(c);
                break;
            case 0x0D:  // DEC C
                c = dec8(c);
                break;
            case 0x0E:  // LD C,n
                c = fetch();
                break;
            case 0x0F: {  // RRCA
                set_c((a & 1) != 0);
                a = uint8_t((a >> 1) | (a << 7));
                set_z(false);
                set_n(false);
                set_h(false);
                break;
            }
            case 0x10: {  // STOP
                fetch();  // discarded operand
                if (change_speed_) {
                    speed_ ^= 1;
                    change_speed_ = false;
                    changed_speed_ = true;
                }
                break;
            }
            case 0x11:  // LD DE,nn
                set_de(fetch16());
                break;
            case 0x12:  // LD (DE),A
                wr(de(), a);
                break;
            case 0x13:  // INC DE
                set_de(uint16_t(de() + 1));
                break;
            case 0x14:  // INC D
                d = inc8(d);
                break;
            case 0x15:  // DEC D
                d = dec8(d);
                break;
            case 0x16:  // LD D,n
                d = fetch();
                break;
            case 0x17: {  // RLA
                uint8_t carry_in = flag_c() ? 1 : 0;
                set_c((a & 0x80) != 0);
                a = uint8_t((a << 1) | carry_in);
                set_z(false);
                set_n(false);
                set_h(false);
                break;
            }
            case 0x18: {  // JR n
                int8_t off = int8_t(fetch());
                pc_ = uint16_t(pc_ + off);
                break;
            }
            case 0x19:  // ADD HL,DE
                add_hl(de());
                break;
            case 0x1A:  // LD A,(DE)
                a = rd(de());
                break;
            case 0x1B:  // DEC DE
                set_de(uint16_t(de() - 1));
                break;
            case 0x1C:  // INC E
                e = inc8(e);
                break;
            case 0x1D:  // DEC E
                e = dec8(e);
                break;
            case 0x1E:  // LD E,n
                e = fetch();
                break;
            case 0x1F: {  // RRA
                uint8_t carry_in = flag_c() ? 0x80 : 0;
                set_c((a & 1) != 0);
                a = uint8_t((a >> 1) | carry_in);
                set_z(false);
                set_n(false);
                set_h(false);
                break;
            }
            case 0x20: {  // JR NZ,n
                int8_t off = int8_t(fetch());
                if (!flag_z()) {
                    pc_ = uint16_t(pc_ + off);
                    cycles_ += 4;
                }
                break;
            }
            case 0x21:  // LD HL,nn
                set_hl(fetch16());
                break;
            case 0x22:  // LD (HL+),A
                wr(hl(), a);
                set_hl(uint16_t(hl() + 1));
                break;
            case 0x23:  // INC HL
                set_hl(uint16_t(hl() + 1));
                break;
            case 0x24:  // INC H
                h = inc8(h);
                break;
            case 0x25:  // DEC H
                h = dec8(h);
                break;
            case 0x26:  // LD H,n
                h = fetch();
                break;
            case 0x27: {  // DAA
                // Game Boy DAA (matches the Pascal / Pan Docs behaviour).
                uint8_t adj = 0;
                bool c = flag_c();
                if (!flag_n()) {
                    if (flag_c() || a > 0x99) {
                        adj |= 0x60;
                        c = true;
                    }
                    if (flag_h() || (a & 0x0f) > 0x09) adj |= 0x06;
                } else {
                    if (flag_c()) adj |= 0x60;
                    if (flag_h()) adj |= 0x06;
                }
                a = uint8_t(a + (flag_n() ? -adj : adj));
                set_z(a == 0);
                set_h(false);
                set_c(c);
                break;
            }
            case 0x28: {  // JR Z,n
                int8_t off = int8_t(fetch());
                if (flag_z()) {
                    pc_ = uint16_t(pc_ + off);
                    cycles_ += 4;
                }
                break;
            }
            case 0x29:  // ADD HL,HL
                add_hl(hl());
                break;
            case 0x2A:  // LD A,(HL+)
                a = rd(hl());
                set_hl(uint16_t(hl() + 1));
                break;
            case 0x2B:  // DEC HL
                set_hl(uint16_t(hl() - 1));
                break;
            case 0x2C:  // INC L
                l = inc8(l);
                break;
            case 0x2D:  // DEC L
                l = dec8(l);
                break;
            case 0x2E:  // LD L,n
                l = fetch();
                break;
            case 0x2F:  // CPL
                a = uint8_t(~a);
                set_n(true);
                set_h(true);
                break;
            case 0x30: {  // JR NC,n
                int8_t off = int8_t(fetch());
                if (!flag_c()) {
                    pc_ = uint16_t(pc_ + off);
                    cycles_ += 4;
                }
                break;
            }
            case 0x31:  // LD SP,nn
                sp = fetch16();
                break;
            case 0x32:  // LD (HL-),A
                wr(hl(), a);
                set_hl(uint16_t(hl() - 1));
                break;
            case 0x33:  // INC SP
                sp = uint16_t(sp + 1);
                break;
            case 0x34: {  // INC (HL)
                uint8_t v = inc8(rd(hl()));
                wr(hl(), v);
                break;
            }
            case 0x35: {  // DEC (HL)
                uint8_t v = dec8(rd(hl()));
                wr(hl(), v);
                break;
            }
            case 0x36:  // LD (HL),n
                wr(hl(), fetch());
                break;
            case 0x37:  // SCF
                set_n(false);
                set_h(false);
                set_c(true);
                break;
            case 0x38: {  // JR C,n
                int8_t off = int8_t(fetch());
                if (flag_c()) {
                    pc_ = uint16_t(pc_ + off);
                    cycles_ += 4;
                }
                break;
            }
            case 0x39:  // ADD HL,SP
                add_hl(sp);
                break;
            case 0x3A:  // LD A,(HL-)
                a = rd(hl());
                set_hl(uint16_t(hl() - 1));
                break;
            case 0x3B:  // DEC SP
                sp = uint16_t(sp - 1);
                break;
            case 0x3C:  // INC A
                a = inc8(a);
                break;
            case 0x3D:  // DEC A
                a = dec8(a);
                break;
            case 0x3E:  // LD A,n
                a = fetch();
                break;
            case 0x3F:  // CCF
                set_c(!flag_c());
                set_n(false);
                set_h(false);
                break;

            // LD r,r' and ALU on registers --------------------------------
            case 0x40: break;  // LD B,B
            case 0x41: b = c; break;
            case 0x42: b = d; break;
            case 0x43: b = e; break;
            case 0x44: b = h; break;
            case 0x45: b = l; break;
            case 0x46: b = rd(hl()); break;
            case 0x47: b = a; break;
            case 0x48: c = b; break;
            case 0x49: break;  // LD C,C
            case 0x4A: c = d; break;
            case 0x4B: c = e; break;
            case 0x4C: c = h; break;
            case 0x4D: c = l; break;
            case 0x4E: c = rd(hl()); break;
            case 0x4F: c = a; break;
            case 0x50: d = b; break;
            case 0x51: d = c; break;
            case 0x52: break;  // LD D,D
            case 0x53: d = e; break;
            case 0x54: d = h; break;
            case 0x55: d = l; break;
            case 0x56: d = rd(hl()); break;
            case 0x57: d = a; break;
            case 0x58: e = b; break;
            case 0x59: e = c; break;
            case 0x5A: e = d; break;
            case 0x5B: break;  // LD E,E
            case 0x5C: e = h; break;
            case 0x5D: e = l; break;
            case 0x5E: e = rd(hl()); break;
            case 0x5F: e = a; break;
            case 0x60: h = b; break;
            case 0x61: h = c; break;
            case 0x62: h = d; break;
            case 0x63: h = e; break;
            case 0x64: break;  // LD H,H
            case 0x65: h = l; break;
            case 0x66: h = rd(hl()); break;
            case 0x67: h = a; break;
            case 0x68: l = b; break;
            case 0x69: l = c; break;
            case 0x6A: l = d; break;
            case 0x6B: l = e; break;
            case 0x6C: l = h; break;
            case 0x6D: break;  // LD L,L
            case 0x6E: l = rd(hl()); break;
            case 0x6F: l = a; break;
            case 0x70: wr(hl(), b); break;
            case 0x71: wr(hl(), c); break;
            case 0x72: wr(hl(), d); break;
            case 0x73: wr(hl(), e); break;
            case 0x74: wr(hl(), h); break;
            case 0x75: wr(hl(), l); break;
            case 0x76:  // HALT
                halted_ = true;
                break;
            case 0x77: wr(hl(), a); break;
            case 0x78: a = b; break;
            case 0x79: a = c; break;
            case 0x7A: a = d; break;
            case 0x7B: a = e; break;
            case 0x7C: a = h; break;
            case 0x7D: a = l; break;
            case 0x7E: a = rd(hl()); break;
            case 0x7F: break;  // LD A,A

            case 0x80: add_a(b); break;
            case 0x81: add_a(c); break;
            case 0x82: add_a(d); break;
            case 0x83: add_a(e); break;
            case 0x84: add_a(h); break;
            case 0x85: add_a(l); break;
            case 0x86: add_a(rd(hl())); break;
            case 0x87: add_a(a); break;
            case 0x88: adc_a(b); break;
            case 0x89: adc_a(c); break;
            case 0x8A: adc_a(d); break;
            case 0x8B: adc_a(e); break;
            case 0x8C: adc_a(h); break;
            case 0x8D: adc_a(l); break;
            case 0x8E: adc_a(rd(hl())); break;
            case 0x8F: adc_a(a); break;
            case 0x90: sub_a(b); break;
            case 0x91: sub_a(c); break;
            case 0x92: sub_a(d); break;
            case 0x93: sub_a(e); break;
            case 0x94: sub_a(h); break;
            case 0x95: sub_a(l); break;
            case 0x96: sub_a(rd(hl())); break;
            case 0x97: sub_a(a); break;
            case 0x98: sbc_a(b); break;
            case 0x99: sbc_a(c); break;
            case 0x9A: sbc_a(d); break;
            case 0x9B: sbc_a(e); break;
            case 0x9C: sbc_a(h); break;
            case 0x9D: sbc_a(l); break;
            case 0x9E: sbc_a(rd(hl())); break;
            case 0x9F: sbc_a(a); break;
            case 0xA0: and_a(b); break;
            case 0xA1: and_a(c); break;
            case 0xA2: and_a(d); break;
            case 0xA3: and_a(e); break;
            case 0xA4: and_a(h); break;
            case 0xA5: and_a(l); break;
            case 0xA6: and_a(rd(hl())); break;
            case 0xA7: and_a(a); break;
            case 0xA8: xor_a(b); break;
            case 0xA9: xor_a(c); break;
            case 0xAA: xor_a(d); break;
            case 0xAB: xor_a(e); break;
            case 0xAC: xor_a(h); break;
            case 0xAD: xor_a(l); break;
            case 0xAE: xor_a(rd(hl())); break;
            case 0xAF: xor_a(a); break;
            case 0xB0: or_a(b); break;
            case 0xB1: or_a(c); break;
            case 0xB2: or_a(d); break;
            case 0xB3: or_a(e); break;
            case 0xB4: or_a(h); break;
            case 0xB5: or_a(l); break;
            case 0xB6: or_a(rd(hl())); break;
            case 0xB7: or_a(a); break;
            case 0xB8: cp_a(b); break;
            case 0xB9: cp_a(c); break;
            case 0xBA: cp_a(d); break;
            case 0xBB: cp_a(e); break;
            case 0xBC: cp_a(h); break;
            case 0xBD: cp_a(l); break;
            case 0xBE: cp_a(rd(hl())); break;
            case 0xBF: cp_a(a); break;

            case 0xC0:  // RET NZ
                if (!flag_z()) {
                    pc_ = pop();
                    cycles_ += 12;
                }
                break;
            case 0xC1:  // POP BC
                set_bc(pop());
                break;
            case 0xC2: {  // JP NZ,nn
                uint16_t addr = fetch16();
                if (!flag_z()) {
                    pc_ = addr;
                    cycles_ += 4;
                }
                break;
            }
            case 0xC3:  // JP nn
                pc_ = fetch16();
                break;
            case 0xC4: {  // CALL NZ,nn
                uint16_t addr = fetch16();
                if (!flag_z()) {
                    push(pc_);
                    pc_ = addr;
                    cycles_ += 12;
                }
                break;
            }
            case 0xC5:  // PUSH BC
                push(bc());
                break;
            case 0xC6:  // ADD A,n
                add_a(fetch());
                break;
            case 0xC7:  // RST 00
                push(pc_);
                pc_ = 0x00;
                break;
            case 0xC8:  // RET Z
                if (flag_z()) {
                    pc_ = pop();
                    cycles_ += 12;
                }
                break;
            case 0xC9:  // RET
                pc_ = pop();
                break;
            case 0xCA: {  // JP Z,nn
                uint16_t addr = fetch16();
                if (flag_z()) {
                    pc_ = addr;
                    cycles_ += 4;
                }
                break;
            }
            case 0xCB:  // CB prefix
                // kMain[0xCB] is 0; exec_cb sets the real cost.
                cycles_ = 0;
                exec_cb();
                break;
            case 0xCC: {  // CALL Z,nn
                uint16_t addr = fetch16();
                if (flag_z()) {
                    push(pc_);
                    pc_ = addr;
                    cycles_ += 12;
                }
                break;
            }
            case 0xCD: {  // CALL nn
                uint16_t addr = fetch16();
                push(pc_);
                pc_ = addr;
                break;
            }
            case 0xCE:  // ADC A,n
                adc_a(fetch());
                break;
            case 0xCF:  // RST 08
                push(pc_);
                pc_ = 0x08;
                break;
            case 0xD0:  // RET NC
                if (!flag_c()) {
                    pc_ = pop();
                    cycles_ += 12;
                }
                break;
            case 0xD1:  // POP DE
                set_de(pop());
                break;
            case 0xD2: {  // JP NC,nn
                uint16_t addr = fetch16();
                if (!flag_c()) {
                    pc_ = addr;
                    cycles_ += 4;
                }
                break;
            }
            case 0xD3:  // illegal
                break;
            case 0xD4: {  // CALL NC,nn
                uint16_t addr = fetch16();
                if (!flag_c()) {
                    push(pc_);
                    pc_ = addr;
                    cycles_ += 12;
                }
                break;
            }
            case 0xD5:  // PUSH DE
                push(de());
                break;
            case 0xD6:  // SUB n
                sub_a(fetch());
                break;
            case 0xD7:  // RST 10
                push(pc_);
                pc_ = 0x10;
                break;
            case 0xD8:  // RET C
                if (flag_c()) {
                    pc_ = pop();
                    cycles_ += 12;
                }
                break;
            case 0xD9:  // RETI
                pc_ = pop();
                ime_ = true;
                break;
            case 0xDA: {  // JP C,nn
                uint16_t addr = fetch16();
                if (flag_c()) {
                    pc_ = addr;
                    cycles_ += 4;
                }
                break;
            }
            case 0xDB:  // illegal
                break;
            case 0xDC: {  // CALL C,nn
                uint16_t addr = fetch16();
                if (flag_c()) {
                    push(pc_);
                    pc_ = addr;
                    cycles_ += 12;
                }
                break;
            }
            case 0xDD:  // illegal
                break;
            case 0xDE:  // SBC A,n
                sbc_a(fetch());
                break;
            case 0xDF:  // RST 18
                push(pc_);
                pc_ = 0x18;
                break;
            case 0xE0:  // LDH (n),A  == LD ($FF00+n),A
                wr(uint16_t(0xFF00 + fetch()), a);
                break;
            case 0xE1:  // POP HL
                set_hl(pop());
                break;
            case 0xE2:  // LD (C),A  == LD ($FF00+C),A
                wr(uint16_t(0xFF00 + c), a);
                break;
            case 0xE3:  // illegal
            case 0xE4:
                break;
            case 0xE5:  // PUSH HL
                push(hl());
                break;
            case 0xE6:  // AND n
                and_a(fetch());
                break;
            case 0xE7:  // RST 20
                push(pc_);
                pc_ = 0x20;
                break;
            case 0xE8: {  // ADD SP,n
                int8_t off = int8_t(fetch());
                unsigned r1 = (sp & 0xff) + uint8_t(off);
                unsigned r2 = (sp & 0x0f) + (uint8_t(off) & 0x0f);
                set_z(false);
                set_n(false);
                set_c(r1 > 0xff);
                set_h(r2 > 0x0f);
                sp = uint16_t(sp + off);
                break;
            }
            case 0xE9:  // JP (HL)
                pc_ = hl();
                break;
            case 0xEA:  // LD (nn),A
                wr(fetch16(), a);
                break;
            case 0xEB:
            case 0xEC:
            case 0xED:
                break;
            case 0xEE:  // XOR n
                xor_a(fetch());
                break;
            case 0xEF:  // RST 28
                push(pc_);
                pc_ = 0x28;
                break;
            case 0xF0:  // LDH A,(n)
                a = rd(uint16_t(0xFF00 + fetch()));
                break;
            case 0xF1: {  // POP AF  (low nibble of F always 0)
                uint16_t v = pop();
                a = uint8_t(v >> 8);
                f = uint8_t(v & 0xF0);
                break;
            }
            case 0xF2:  // LD A,(C)
                a = rd(uint16_t(0xFF00 + c));
                break;
            case 0xF3:  // DI
                ime_ = false;
                break;
            case 0xF4:
                break;
            case 0xF5:  // PUSH AF
                push(uint16_t((a << 8) | (f & 0xF0)));
                break;
            case 0xF6:  // OR n
                or_a(fetch());
                break;
            case 0xF7:  // RST 30
                push(pc_);
                pc_ = 0x30;
                break;
            case 0xF8: {  // LD HL,SP+n
                int8_t off = int8_t(fetch());
                unsigned r1 = (sp & 0xff) + uint8_t(off);
                unsigned r2 = (sp & 0x0f) + (uint8_t(off) & 0x0f);
                set_z(false);
                set_n(false);
                set_c(r1 > 0xff);
                set_h(r2 > 0x0f);
                set_hl(uint16_t(sp + off));
                break;
            }
            case 0xF9:  // LD SP,HL
                sp = hl();
                break;
            case 0xFA:  // LD A,(nn)
                a = rd(fetch16());
                break;
            case 0xFB:  // EI
                ime_ = true;
                after_ei_ = true;
                break;
            case 0xFC:
            case 0xFD:
                break;
            case 0xFE:  // CP n
                cp_a(fetch());
                break;
            case 0xFF:  // RST 38
                push(pc_);
                pc_ = 0x38;
                break;
        }

        // Deliver cycles in 4-T slices so the GB timer / LCD can tick mid-op
        // the same way the Pascal core does (despues_instruccion every 4 T).
        if (cycle_handler_) {
            for (int left = cycles_; left > 0; left -= 4) {
                const int slice = left >= 4 ? 4 : left;
                cycle_handler_(slice);
            }
        }
        executed_ += cycles_;
    }
    return executed_;
}

}  // namespace dsp
