#include "cpu/spc700.h"

namespace dsp {
namespace {
// Cycle counts per opcode, from the published timing table.
const uint8_t kCycles[256] = {
    2,8,4,5,3,4,3,6,2,6,5,4,5,4,6,8,   // 00
    2,8,4,5,4,5,5,6,5,5,6,5,2,2,4,6,   // 10
    2,8,4,5,3,4,3,6,2,6,5,4,5,4,5,4,   // 20
    2,8,4,5,4,5,5,6,5,5,6,5,2,2,3,8,   // 30
    2,8,4,5,3,4,3,6,2,6,4,4,5,4,6,6,   // 40
    2,8,4,5,4,5,5,6,5,5,4,5,2,2,4,3,   // 50
    2,8,4,5,3,4,3,6,2,6,4,4,5,4,5,5,   // 60
    2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,6,   // 70
    2,8,4,5,3,4,3,6,2,6,5,4,5,2,4,5,   // 80
    2,8,4,5,4,5,5,6,5,5,5,5,2,2,12,5,  // 90
    3,8,4,5,3,4,3,6,2,6,4,4,5,2,4,4,   // A0
    2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,4,   // B0
    3,8,4,5,4,5,4,7,2,5,6,4,5,2,4,9,   // C0
    2,8,4,5,5,6,6,7,4,5,5,5,2,2,6,3,   // D0
    2,8,4,5,3,4,3,6,2,4,5,3,4,3,4,3,   // E0
    2,8,4,5,4,5,5,6,3,4,5,4,2,2,4,3,   // F0
};
}  // namespace

void Spc700::reset() {
    a = x = y = 0;
    sp = 0xef;
    psw_ = Psw{};
    // The reset vector lives at the top of the IPL ROM.
    pc_ = uint16_t(rd(0xfffe) | (rd(0xffff) << 8));
}

uint8_t Spc700::psw_byte() const {
    return uint8_t((psw_.n ? 0x80 : 0) | (psw_.v ? 0x40 : 0) | (psw_.p ? 0x20 : 0) |
                   (psw_.b ? 0x10 : 0) | (psw_.h ? 0x08 : 0) | (psw_.i ? 0x04 : 0) |
                   (psw_.z ? 0x02 : 0) | (psw_.c ? 0x01 : 0));
}

void Spc700::set_psw(uint8_t v) {
    psw_.n = v & 0x80; psw_.v = v & 0x40; psw_.p = v & 0x20; psw_.b = v & 0x10;
    psw_.h = v & 0x08; psw_.i = v & 0x04; psw_.z = v & 0x02; psw_.c = v & 0x01;
}

uint8_t Spc700::op_adc(uint8_t m, uint8_t n) {
    const int r = m + n + (psw_.c ? 1 : 0);
    psw_.c = r > 0xff;
    psw_.h = ((m ^ n ^ uint8_t(r)) & 0x10) != 0;
    psw_.v = (~(m ^ n) & (m ^ uint8_t(r)) & 0x80) != 0;
    return setnz(uint8_t(r));
}

uint8_t Spc700::op_sbc(uint8_t m, uint8_t n) {
    const int r = m - n - (psw_.c ? 0 : 1);
    psw_.c = r >= 0;
    psw_.h = ((m ^ n ^ uint8_t(r)) & 0x10) == 0;
    psw_.v = ((m ^ n) & (m ^ uint8_t(r)) & 0x80) != 0;
    return setnz(uint8_t(r));
}

void Spc700::op_cmp(uint8_t m, uint8_t n) {
    const int r = m - n;
    psw_.c = r >= 0;
    setnz(uint8_t(r));
}

uint8_t Spc700::op_asl(uint8_t v) { psw_.c = (v & 0x80) != 0; return setnz(uint8_t(v << 1)); }
uint8_t Spc700::op_lsr(uint8_t v) { psw_.c = (v & 0x01) != 0; return setnz(uint8_t(v >> 1)); }
uint8_t Spc700::op_rol(uint8_t v) {
    const uint8_t c = psw_.c ? 1 : 0;
    psw_.c = (v & 0x80) != 0;
    return setnz(uint8_t((v << 1) | c));
}
uint8_t Spc700::op_ror(uint8_t v) {
    const uint8_t c = psw_.c ? 0x80 : 0;
    psw_.c = (v & 0x01) != 0;
    return setnz(uint8_t((v >> 1) | c));
}

void Spc700::branch(bool take, int& cycles) {
    const int8_t rel = int8_t(fetch());
    if (take) { pc_ = uint16_t(pc_ + rel); cycles += 2; }
}

int Spc700::step() {
    const uint8_t op = fetch();
    int cycles = kCycles[op];

    // Addressing helpers used by the regular families below.
    auto addr_dp = [&]() { return dp(fetch()); };
    auto addr_dpx = [&]() { return dp(uint8_t(fetch() + x)); };
    auto addr_dpy = [&]() { return dp(uint8_t(fetch() + y)); };
    auto addr_abs = [&]() { return fetch16(); };
    auto addr_absx = [&]() { return uint16_t(fetch16() + x); };
    auto addr_absy = [&]() { return uint16_t(fetch16() + y); };
    auto addr_idx = [&]() {           // [dp+X] : pointer then fetch
        const uint16_t p = dp(uint8_t(fetch() + x));
        return uint16_t(rd(p) | (rd(uint16_t(p + 1)) << 8));
    };
    auto addr_idy = [&]() {           // [dp]+Y
        const uint16_t p = addr_dp();
        return uint16_t((rd(p) | (rd(uint16_t(p + 1)) << 8)) + y);
    };
    // dp.bit addressing packs the bit number into the opcode's top three bits.
    auto bit_addr = [&](uint8_t& bit) {
        const uint16_t v = fetch16();
        bit = uint8_t(v >> 13);
        return uint16_t(v & 0x1fff);
    };

    switch (op) {
        case 0x00: break;                                    // NOP
        case 0xef: case 0xff: pc_--; break;                  // SLEEP / STOP: hold
        case 0x8f: { const uint8_t v = fetch(); wr(addr_dp(), v); break; }   // MOV dp,#i
        case 0xfa: { const uint16_t s = addr_dp(); wr(addr_dp(), rd(s)); break; }  // MOV dd,ds

        // --- MOV A ---
        case 0xe8: setnz(a = fetch()); break;
        case 0xe4: setnz(a = rd(addr_dp())); break;
        case 0xf4: setnz(a = rd(addr_dpx())); break;
        case 0xe5: setnz(a = rd(addr_abs())); break;
        case 0xf5: setnz(a = rd(addr_absx())); break;
        case 0xf6: setnz(a = rd(addr_absy())); break;
        case 0xe6: setnz(a = rd(dp(x))); break;
        case 0xbf: setnz(a = rd(dp(x))); x++; break;         // MOV A,(X)+
        case 0xe7: setnz(a = rd(addr_idx())); break;
        case 0xf7: setnz(a = rd(addr_idy())); break;
        case 0x7d: setnz(a = x); break;                      // MOV A,X
        case 0xdd: setnz(a = y); break;                      // MOV A,Y

        case 0xc4: wr(addr_dp(), a); break;
        case 0xd4: wr(addr_dpx(), a); break;
        case 0xc5: wr(addr_abs(), a); break;
        case 0xd5: wr(addr_absx(), a); break;
        case 0xd6: wr(addr_absy(), a); break;
        case 0xc6: wr(dp(x), a); break;
        case 0xaf: wr(dp(x), a); x++; break;                 // MOV (X)+,A
        case 0xc7: wr(addr_idx(), a); break;
        case 0xd7: wr(addr_idy(), a); break;
        case 0x5d: setnz(x = a); break;                      // MOV X,A
        case 0xfd: setnz(y = a); break;                      // MOV Y,A

        // --- MOV X / Y ---
        case 0xcd: setnz(x = fetch()); break;
        case 0xf8: setnz(x = rd(addr_dp())); break;
        case 0xf9: setnz(x = rd(addr_dpy())); break;
        case 0xe9: setnz(x = rd(addr_abs())); break;
        case 0xd8: wr(addr_dp(), x); break;
        case 0xd9: wr(addr_dpy(), x); break;
        case 0xc9: wr(addr_abs(), x); break;
        case 0x9d: setnz(x = sp); break;                     // MOV X,SP
        case 0xbd: sp = x; break;                            // MOV SP,X

        case 0x8d: setnz(y = fetch()); break;
        case 0xeb: setnz(y = rd(addr_dp())); break;
        case 0xfb: setnz(y = rd(addr_dpx())); break;
        case 0xec: setnz(y = rd(addr_abs())); break;
        case 0xcb: wr(addr_dp(), y); break;
        case 0xdb: wr(addr_dpx(), y); break;
        case 0xcc: wr(addr_abs(), y); break;

        // --- arithmetic/logic on A, one family per operation ---
        #define ALU_A(base, EXPR)                                            \
            case base + 0x08: { const uint8_t m = fetch();      EXPR; break; } \
            case base + 0x06: { const uint8_t m = rd(dp(x));    EXPR; break; } \
            case base + 0x04: { const uint8_t m = rd(addr_dp());  EXPR; break; } \
            case base + 0x14: { const uint8_t m = rd(addr_dpx()); EXPR; break; } \
            case base + 0x05: { const uint8_t m = rd(addr_abs()); EXPR; break; } \
            case base + 0x15: { const uint8_t m = rd(addr_absx());EXPR; break; } \
            case base + 0x16: { const uint8_t m = rd(addr_absy());EXPR; break; } \
            case base + 0x07: { const uint8_t m = rd(addr_idx()); EXPR; break; } \
            case base + 0x17: { const uint8_t m = rd(addr_idy()); EXPR; break; }
        ALU_A(0x00, setnz(a = uint8_t(a | m)))      // OR
        ALU_A(0x20, setnz(a = uint8_t(a & m)))      // AND
        ALU_A(0x40, setnz(a = uint8_t(a ^ m)))      // EOR
        ALU_A(0x60, op_cmp(a, m))                   // CMP
        ALU_A(0x80, a = op_adc(a, m))               // ADC
        ALU_A(0xa0, a = op_sbc(a, m))               // SBC
        #undef ALU_A

        // (X),(Y) and dp,dp forms
        case 0x19: { const uint8_t r = uint8_t(rd(dp(x)) | rd(dp(y))); wr(dp(x), setnz(r)); break; }
        case 0x39: { const uint8_t r = uint8_t(rd(dp(x)) & rd(dp(y))); wr(dp(x), setnz(r)); break; }
        case 0x59: { const uint8_t r = uint8_t(rd(dp(x)) ^ rd(dp(y))); wr(dp(x), setnz(r)); break; }
        case 0x79: op_cmp(rd(dp(x)), rd(dp(y))); break;
        case 0x99: { const uint8_t r = op_adc(rd(dp(x)), rd(dp(y))); wr(dp(x), r); break; }
        case 0xb9: { const uint8_t r = op_sbc(rd(dp(x)), rd(dp(y))); wr(dp(x), r); break; }
        case 0x09: { const uint8_t s = rd(addr_dp()); const uint16_t d = addr_dp();
                     wr(d, setnz(uint8_t(rd(d) | s))); break; }
        case 0x29: { const uint8_t s = rd(addr_dp()); const uint16_t d = addr_dp();
                     wr(d, setnz(uint8_t(rd(d) & s))); break; }
        case 0x49: { const uint8_t s = rd(addr_dp()); const uint16_t d = addr_dp();
                     wr(d, setnz(uint8_t(rd(d) ^ s))); break; }
        case 0x69: { const uint8_t s = rd(addr_dp()); op_cmp(rd(addr_dp()), s); break; }
        case 0x89: { const uint8_t s = rd(addr_dp()); const uint16_t d = addr_dp();
                     wr(d, op_adc(rd(d), s)); break; }
        case 0xa9: { const uint8_t s = rd(addr_dp()); const uint16_t d = addr_dp();
                     wr(d, op_sbc(rd(d), s)); break; }
        case 0x98: { const uint8_t s = fetch(); const uint16_t d = addr_dp();
                     wr(d, op_adc(rd(d), s)); break; }
        case 0xb8: { const uint8_t s = fetch(); const uint16_t d = addr_dp();
                     wr(d, op_sbc(rd(d), s)); break; }
        case 0x18: { const uint8_t s = fetch(); const uint16_t d = addr_dp();
                     wr(d, setnz(uint8_t(rd(d) | s))); break; }
        case 0x38: { const uint8_t s = fetch(); const uint16_t d = addr_dp();
                     wr(d, setnz(uint8_t(rd(d) & s))); break; }
        case 0x58: { const uint8_t s = fetch(); const uint16_t d = addr_dp();
                     wr(d, setnz(uint8_t(rd(d) ^ s))); break; }
        case 0x78: { const uint8_t s = fetch(); op_cmp(rd(addr_dp()), s); break; }

        // CMP X / CMP Y
        case 0xc8: op_cmp(x, fetch()); break;
        case 0x3e: op_cmp(x, rd(addr_dp())); break;
        case 0x1e: op_cmp(x, rd(addr_abs())); break;
        case 0xad: op_cmp(y, fetch()); break;
        case 0x7e: op_cmp(y, rd(addr_dp())); break;
        case 0x5e: op_cmp(y, rd(addr_abs())); break;

        // --- shifts and increments ---
        #define RMW(opc_a, opc_dp, opc_dpx, opc_abs, FN)                       \
            case opc_a:   a = FN(a); break;                                    \
            case opc_dp:  { const uint16_t t = addr_dp();  wr(t, FN(rd(t))); break; } \
            case opc_dpx: { const uint16_t t = addr_dpx(); wr(t, FN(rd(t))); break; } \
            case opc_abs: { const uint16_t t = addr_abs(); wr(t, FN(rd(t))); break; }
        RMW(0x1c, 0x0b, 0x1b, 0x0c, op_asl)
        RMW(0x5c, 0x4b, 0x5b, 0x4c, op_lsr)
        RMW(0x3c, 0x2b, 0x3b, 0x2c, op_rol)
        RMW(0x7c, 0x6b, 0x7b, 0x6c, op_ror)
        RMW(0xbc, 0xab, 0xbb, 0xac, [&](uint8_t v) { return setnz(uint8_t(v + 1)); })
        RMW(0x9c, 0x8b, 0x9b, 0x8c, [&](uint8_t v) { return setnz(uint8_t(v - 1)); })
        #undef RMW
        case 0x3d: setnz(++x); break;
        case 0x1d: setnz(--x); break;
        case 0xfc: setnz(++y); break;
        case 0xdc: setnz(--y); break;

        // --- 16-bit (YA and word memory) ---
        case 0xba: { const uint16_t t = addr_dp();            // MOVW YA,dp
                     a = rd(t); y = rd(uint16_t(t + 1));
                     setnz16(uint16_t(a | (y << 8))); break; }
        case 0xda: { const uint16_t t = addr_dp();            // MOVW dp,YA
                     wr(t, a); wr(uint16_t(t + 1), y); break; }
        case 0x3a: { const uint16_t t = addr_dp();            // INCW dp
                     uint16_t v = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8));
                     v++; wr(t, uint8_t(v)); wr(uint16_t(t + 1), uint8_t(v >> 8));
                     setnz16(v); break; }
        case 0x1a: { const uint16_t t = addr_dp();            // DECW dp
                     uint16_t v = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8));
                     v--; wr(t, uint8_t(v)); wr(uint16_t(t + 1), uint8_t(v >> 8));
                     setnz16(v); break; }
        case 0x7a: { const uint16_t t = addr_dp();            // ADDW YA,dp
                     const uint16_t m = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8));
                     const uint16_t ya = uint16_t(a | (y << 8));
                     const uint32_t r = uint32_t(ya) + m;
                     psw_.c = r > 0xffff;
                     psw_.h = ((ya ^ m ^ uint16_t(r)) & 0x1000) != 0;
                     psw_.v = (~(ya ^ m) & (ya ^ uint16_t(r)) & 0x8000) != 0;
                     a = uint8_t(r); y = uint8_t(r >> 8); setnz16(uint16_t(r)); break; }
        case 0x9a: { const uint16_t t = addr_dp();            // SUBW YA,dp
                     const uint16_t m = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8));
                     const uint16_t ya = uint16_t(a | (y << 8));
                     const int32_t r = int32_t(ya) - m;
                     psw_.c = r >= 0;
                     psw_.h = ((ya ^ m ^ uint16_t(r)) & 0x1000) == 0;
                     psw_.v = ((ya ^ m) & (ya ^ uint16_t(r)) & 0x8000) != 0;
                     a = uint8_t(r); y = uint8_t(uint16_t(r) >> 8); setnz16(uint16_t(r)); break; }
        case 0x5a: { const uint16_t t = addr_dp();            // CMPW YA,dp
                     const uint16_t m = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8));
                     const uint16_t ya = uint16_t(a | (y << 8));
                     const int32_t r = int32_t(ya) - m;
                     psw_.c = r >= 0; setnz16(uint16_t(r)); break; }
        case 0xcf: { const uint16_t r = uint16_t(a) * y;      // MUL YA
                     a = uint8_t(r); y = uint8_t(r >> 8); setnz(y); break; }
        case 0x9e: {                                          // DIV YA,X
                     const uint16_t ya = uint16_t(a | (y << 8));
                     psw_.h = (x & 0x0f) <= (y & 0x0f);
                     psw_.v = y >= x;
                     if (x == 0) { a = 0xff; y = 0; }
                     else { a = uint8_t(ya / x); y = uint8_t(ya % x); }
                     setnz(a); break; }

        // --- branches ---
        case 0x2f: { const int8_t r = int8_t(fetch()); pc_ = uint16_t(pc_ + r); break; }  // BRA
        case 0xf0: branch(psw_.z, cycles); break;
        case 0xd0: branch(!psw_.z, cycles); break;
        case 0xb0: branch(psw_.c, cycles); break;
        case 0x90: branch(!psw_.c, cycles); break;
        case 0x70: branch(psw_.v, cycles); break;
        case 0x50: branch(!psw_.v, cycles); break;
        case 0x30: branch(psw_.n, cycles); break;
        case 0x10: branch(!psw_.n, cycles); break;
        case 0x2e: { const uint8_t m = rd(addr_dp()); branch(a == m, cycles); break; }  // CBNE? (BEQ dp)
        case 0xde: { const uint8_t m = rd(addr_dpx()); branch(a != m, cycles); break; }
        case 0x6e: { const uint16_t t = addr_dp();            // DBNZ dp,rel
                     const uint8_t v = uint8_t(rd(t) - 1); wr(t, v);
                     branch(v != 0, cycles); break; }
        case 0xfe: { y--; branch(y != 0, cycles); break; }    // DBNZ Y,rel

        // --- calls and returns ---
        case 0x3f: { const uint16_t t = fetch16();            // CALL
                     push(uint8_t(pc_ >> 8)); push(uint8_t(pc_)); pc_ = t; break; }
        case 0x4f: { const uint8_t t = fetch();               // PCALL
                     push(uint8_t(pc_ >> 8)); push(uint8_t(pc_));
                     pc_ = uint16_t(0xff00 | t); break; }
        case 0x6f: { const uint8_t l = pop(); pc_ = uint16_t(l | (pop() << 8)); break; }  // RET
        case 0x7f: { set_psw(pop()); const uint8_t l = pop();
                     pc_ = uint16_t(l | (pop() << 8)); break; }                           // RETI
        case 0x5f: pc_ = fetch16(); break;                    // JMP !a
        case 0x1f: { const uint16_t t = uint16_t(fetch16() + x);   // JMP [!a+X]
                     pc_ = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8)); break; }
        case 0x0f: { push(uint8_t(pc_ >> 8)); push(uint8_t(pc_)); push(psw_byte());
                     psw_.b = true; psw_.i = false;
                     pc_ = uint16_t(rd(0xffde) | (rd(0xffdf) << 8)); break; }             // BRK

        // --- stack ---
        case 0x2d: push(a); break;
        case 0x4d: push(x); break;
        case 0x6d: push(y); break;
        case 0x0d: push(psw_byte()); break;
        case 0xae: a = pop(); break;
        case 0xce: x = pop(); break;
        case 0xee: y = pop(); break;
        case 0x8e: set_psw(pop()); break;

        // --- flags ---
        case 0x60: psw_.c = false; break;
        case 0x80: psw_.c = true; break;
        case 0xed: psw_.c = !psw_.c; break;
        case 0xe0: psw_.v = false; psw_.h = false; break;
        case 0x20: psw_.p = false; break;
        case 0x40: psw_.p = true; break;
        case 0xa0: psw_.i = true; break;
        case 0xc0: psw_.i = false; break;
        case 0x9f: a = uint8_t((a >> 4) | (a << 4)); setnz(a); break;   // XCN
        case 0xdf: if ((a & 0x0f) > 9 || psw_.h) { a = uint8_t(a + 6); }       // DAA
                   if (a > 0x99 || psw_.c) { a = uint8_t(a + 0x60); psw_.c = true; }
                   setnz(a); break;
        case 0xbe: if ((a & 0x0f) > 9 || psw_.h) { a = uint8_t(a - 6); }       // DAS
                   if (a > 0x99 || !psw_.c) { a = uint8_t(a - 0x60); psw_.c = false; }
                   setnz(a); break;

        // --- single-bit operations on dp ---
        case 0x02: case 0x22: case 0x42: case 0x62:
        case 0x82: case 0xa2: case 0xc2: case 0xe2: {          // SET1 dp.bit
            const uint16_t t = addr_dp();
            wr(t, uint8_t(rd(t) | (1 << (op >> 5))));
            break;
        }
        case 0x12: case 0x32: case 0x52: case 0x72:
        case 0x92: case 0xb2: case 0xd2: case 0xf2: {          // CLR1 dp.bit
            const uint16_t t = addr_dp();
            wr(t, uint8_t(rd(t) & ~(1 << (op >> 5))));
            break;
        }
        case 0x03: case 0x23: case 0x43: case 0x63:
        case 0x83: case 0xa3: case 0xc3: case 0xe3: {          // BBS dp.bit,rel
            const uint8_t v = rd(addr_dp());
            branch((v & (1 << (op >> 5))) != 0, cycles);
            break;
        }
        case 0x13: case 0x33: case 0x53: case 0x73:
        case 0x93: case 0xb3: case 0xd3: case 0xf3: {          // BBC dp.bit,rel
            const uint8_t v = rd(addr_dp());
            branch((v & (1 << (op >> 5))) == 0, cycles);
            break;
        }

        // --- carry/bit against an absolute bit address ---
        case 0x4a: { uint8_t b; const uint16_t t = bit_addr(b);           // AND1 C,m.b
                     psw_.c = psw_.c && ((rd(t) >> b) & 1); break; }
        case 0x6a: { uint8_t b; const uint16_t t = bit_addr(b);           // AND1 C,/m.b
                     psw_.c = psw_.c && !((rd(t) >> b) & 1); break; }
        case 0x0a: { uint8_t b; const uint16_t t = bit_addr(b);           // OR1
                     psw_.c = psw_.c || ((rd(t) >> b) & 1); break; }
        case 0x2a: { uint8_t b; const uint16_t t = bit_addr(b);           // OR1 /m.b
                     psw_.c = psw_.c || !((rd(t) >> b) & 1); break; }
        case 0x8a: { uint8_t b; const uint16_t t = bit_addr(b);           // EOR1
                     psw_.c = psw_.c != (((rd(t) >> b) & 1) != 0); break; }
        case 0xaa: { uint8_t b; const uint16_t t = bit_addr(b);           // MOV1 C,m.b
                     psw_.c = ((rd(t) >> b) & 1) != 0; break; }
        case 0xca: { uint8_t b; const uint16_t t = bit_addr(b);           // MOV1 m.b,C
                     const uint8_t v = rd(t);
                     wr(t, uint8_t(psw_.c ? (v | (1 << b)) : (v & ~(1 << b)))); break; }
        case 0xea: { uint8_t b; const uint16_t t = bit_addr(b);           // NOT1 m.b
                     wr(t, uint8_t(rd(t) ^ (1 << b))); break; }

        // --- TSET1 / TCLR1 ---
        case 0x0e: { const uint16_t t = addr_abs(); const uint8_t v = rd(t);
                     setnz(uint8_t(a - v)); wr(t, uint8_t(v | a)); break; }
        case 0x4e: { const uint16_t t = addr_abs(); const uint8_t v = rd(t);
                     setnz(uint8_t(a - v)); wr(t, uint8_t(v & ~a)); break; }

        default:
            // Any remaining encoding behaves as a no-operation rather than
            // stopping the core dead.
            break;
    }
    return cycles;
}

}  // namespace dsp
