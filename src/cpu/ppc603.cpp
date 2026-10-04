#include "cpu/ppc603.h"

#include <cmath>
#include <limits>

namespace dsp {
namespace {

inline uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
inline uint16_t bswap16(uint16_t v) { return __builtin_bswap16(v); }
inline uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }
inline uint32_t rotl(uint32_t v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
inline uint32_t mask(int mb, int me) {
    const uint32_t a = 0xffffffffu >> mb;
    const uint32_t b = 0xffffffffu << (31 - me);
    return mb <= me ? (a & b) : (a | b);
}

constexpr uint32_t MSR_EE = 0x8000, MSR_FP = 0x2000, MSR_IP = 0x40, MSR_ILE = 0x10000;

}  // namespace

void Ppc603::add_fast_region(uint32_t start, uint32_t size, uint8_t* data, bool writable) {
    if (num_regions_ >= int(regions_.size())) return;
    regions_[size_t(num_regions_++)] = Region{start, start + size - 1, data, writable};
}

void Ppc603::reset() {
    r_.fill(0);
    f_.fill(0);
    cr_ = lr_ = ctr_ = 0;
    xer_so_ = xer_ov_ = xer_ca_ = false;
    xer_bc_ = 0;
    fpscr_ = 0;
    srr0_ = srr1_ = dar_ = dsisr_ = sdr1_ = 0;
    sprg_.fill(0);
    sr_.fill(0);
    bat_.fill(0);
    hid0_ = hid1_ = hid2_ = 0;
    msr_ = MSR_IP;
    pc_ = 0xfff00100;
    tb_base_ = 0;
    tb_cycle_ = total_cycles_;
    dec_base_ = 0xffffffff;
    dec_cycle_ = total_cycles_;
    dec_pending_ = false;
    dec_fire_cycle_ = ~0ull;
    reserve_ = false;
}

// ---------------------------------------------------------------------------
// Memory

uint32_t Ppc603::fetch(uint32_t a) {
    if (const Region* r = find(a)) {
        uint32_t v;
        std::memcpy(&v, r->data + (a - r->start), 4);
        return bswap32(v);
    }
    return bus_.read32(a);
}

uint8_t Ppc603::rd8(uint32_t a) {
    if (const Region* r = find(a)) return r->data[a - r->start];
    return bus_.read8(a);
}
uint16_t Ppc603::rd16(uint32_t a) {
    if (const Region* r = find(a)) {
        if (a + 1 <= r->end) {
            uint16_t v;
            std::memcpy(&v, r->data + (a - r->start), 2);
            return bswap16(v);
        }
    }
    return bus_.read16(a);
}
uint32_t Ppc603::rd32(uint32_t a) {
    if (const Region* r = find(a)) {
        if (a + 3 <= r->end) {
            uint32_t v;
            std::memcpy(&v, r->data + (a - r->start), 4);
            return bswap32(v);
        }
    }
    return bus_.read32(a);
}
uint64_t Ppc603::rd64(uint32_t a) {
    if (const Region* r = find(a)) {
        if (a + 7 <= r->end) {
            uint64_t v;
            std::memcpy(&v, r->data + (a - r->start), 8);
            return bswap64(v);
        }
    }
    return bus_.read64(a);
}
void Ppc603::wr8(uint32_t a, uint8_t v) {
    if (const Region* r = find(a)) {
        if (r->writable) r->data[a - r->start] = v;
        return;
    }
    bus_.write8(a, v);
}
void Ppc603::wr16(uint32_t a, uint16_t v) {
    if (const Region* r = find(a)) {
        if (a + 1 <= r->end) {
            if (r->writable) {
                const uint16_t s = bswap16(v);
                std::memcpy(r->data + (a - r->start), &s, 2);
            }
            return;
        }
    }
    bus_.write16(a, v);
}
void Ppc603::wr32(uint32_t a, uint32_t v) {
    if (const Region* r = find(a)) {
        if (a + 3 <= r->end) {
            if (r->writable) {
                const uint32_t s = bswap32(v);
                std::memcpy(r->data + (a - r->start), &s, 4);
            }
            return;
        }
    }
    bus_.write32(a, v);
}
void Ppc603::wr64(uint32_t a, uint64_t v) {
    if (const Region* r = find(a)) {
        if (a + 7 <= r->end) {
            if (r->writable) {
                const uint64_t s = bswap64(v);
                std::memcpy(r->data + (a - r->start), &s, 8);
            }
            return;
        }
    }
    bus_.write64(a, v);
}

// ---------------------------------------------------------------------------
// SPRs, timers, exceptions

uint32_t Ppc603::xer() const {
    return (xer_so_ ? 0x80000000u : 0) | (xer_ov_ ? 0x40000000u : 0) | (xer_ca_ ? 0x20000000u : 0) |
           (xer_bc_ & 0x7f);
}

void Ppc603::set_xer(uint32_t v) {
    xer_so_ = v & 0x80000000u;
    xer_ov_ = v & 0x40000000u;
    xer_ca_ = v & 0x20000000u;
    xer_bc_ = v & 0x7f;
}

uint64_t Ppc603::read_tb() { return tb_base_ + (total_cycles_ - tb_cycle_) / uint64_t(timer_div_); }

void Ppc603::write_tb(uint64_t v) {
    tb_base_ = v;
    tb_cycle_ = total_cycles_;
}

uint32_t Ppc603::read_dec() {
    return dec_base_ - uint32_t((total_cycles_ - dec_cycle_) / uint64_t(timer_div_));
}

void Ppc603::write_dec(uint32_t v) {
    dec_base_ = v;
    dec_cycle_ = total_cycles_;
    // The exception is requested when the decrementer goes from 0 to -1.
    if (!(v & 0x80000000u))
        dec_fire_cycle_ = total_cycles_ + (uint64_t(v) + 1) * uint64_t(timer_div_);
    else
        dec_fire_cycle_ = ~0ull;
}

uint32_t Ppc603::read_spr(int spr) {
    switch (spr) {
        case 1: return xer();
        case 8: return lr_;
        case 9: return ctr_;
        case 18: return dsisr_;
        case 19: return dar_;
        case 22: return read_dec();
        case 25: return sdr1_;
        case 26: return srr0_;
        case 27: return srr1_;
        case 268: case 284: return uint32_t(read_tb());
        case 269: case 285: return uint32_t(read_tb() >> 32);
        case 272: case 273: case 274: case 275: return sprg_[size_t(spr - 272)];
        case 282: return ear_;
        case 287: return pvr_;
        case 1008: return hid0_;
        case 1009: return hid1_;
        case 1010: return hid2_;
        default:
            if (spr >= 528 && spr <= 543) return bat_[size_t(spr - 528)];
            if (spr >= 976 && spr <= 981) return spr603_[size_t(spr - 976)];
            return 0;
    }
}

void Ppc603::write_spr(int spr, uint32_t v) {
    switch (spr) {
        case 1: set_xer(v); break;
        case 8: lr_ = v; break;
        case 9: ctr_ = v; break;
        case 18: dsisr_ = v; break;
        case 19: dar_ = v; break;
        case 22: write_dec(v); break;
        case 25: sdr1_ = v; break;
        case 26: srr0_ = v; break;
        case 27: srr1_ = v; break;
        case 272: case 273: case 274: case 275: sprg_[size_t(spr - 272)] = v; break;
        case 282: ear_ = v; break;
        case 284: write_tb((read_tb() & 0xffffffff00000000ull) | v); break;
        case 285: write_tb((read_tb() & 0xffffffffull) | (uint64_t(v) << 32)); break;
        case 1008: hid0_ = v; break;
        case 1009: hid1_ = v; break;
        case 1010: hid2_ = v; break;
        default:
            if (spr >= 528 && spr <= 543) bat_[size_t(spr - 528)] = v;
            else if (spr >= 976 && spr <= 981) spr603_[size_t(spr - 976)] = v;
            break;
    }
}

void Ppc603::exception(uint32_t vector, uint32_t srr0, uint32_t srr1_bits) {
    srr0_ = srr0;
    srr1_ = (msr_ & 0x87c0ffffu) | srr1_bits;
    uint32_t m = msr_ & ~(0x8000u | 0x4000u | 0x2000u | 0x800u | 0x400u | 0x200u | 0x100u | 0x20u | 0x10u |
                          0x2u | 0x40000u | 0x1u);
    if (msr_ & MSR_ILE) m |= 1;
    msr_ = m;
    pc_ = ((msr_ & MSR_IP) ? 0xfff00000u : 0) | vector;
    reserve_ = false;
}

void Ppc603::check_interrupts() {
    if (!(msr_ & MSR_EE)) return;
    if (irq_line_) {
        exception(0x500, pc_);
    } else if (dec_pending_) {
        dec_pending_ = false;
        exception(0x900, pc_);
    }
}

bool Ppc603::fp_available() {
    if (msr_ & MSR_FP) return true;
    exception(0x800, cur_pc_);
    return false;
}

void Ppc603::illegal(uint32_t /*op*/) { exception(0x700, cur_pc_, 0x80000); }

// ---------------------------------------------------------------------------

// A short backward loop that only loads, compares and branches cannot
// change anything by itself: it is waiting for an interrupt or for another
// device, so the rest of the time slice can be skipped.
bool Ppc603::is_spin_loop(uint32_t start, uint32_t branch) {
    for (uint32_t a = start; a <= branch; a += 4) {
        const uint32_t op = fetch(a);
        const uint32_t primary = op >> 26;
        switch (primary) {
            case 10: case 11:                 // cmpli, cmpi
            case 21:                          // rlwinm
            case 28: case 29:                 // andi., andis.
            case 32: case 34: case 40: case 42:  // lwz, lbz, lhz, lha
                break;
            case 16:                          // bc: must not touch CTR
                if (((op >> 21) & 4) == 0) return false;
                break;
            case 18: break;                   // b
            case 31: {
                const uint32_t xo = (op >> 1) & 0x3ff;
                if (xo != 0 && xo != 32 && xo != 23 && xo != 87 && xo != 279 && xo != 28) return false;
                break;
            }
            default: return false;
        }
    }
    return true;
}

int Ppc603::run(int cycles) {
    budget_ = cycles;
    stop_ = false;
    int done = 0;
    while (done < budget_ && !stop_) {
        if (total_cycles_ >= dec_fire_cycle_) {
            dec_fire_cycle_ = ~0ull;
            dec_pending_ = true;
        }
        if ((irq_line_ || dec_pending_) && (msr_ & MSR_EE)) check_interrupts();
        cur_pc_ = pc_;
        const uint32_t op = fetch(pc_);
        pc_ += 4;
        exec(op);
        ++done;
        ++total_cycles_;
        bool spin = false;
        if (pc_ < cur_pc_ && cur_pc_ - pc_ <= 32 && spin_detect_) {
            if (pc_ != spin_target_ || cur_pc_ != spin_branch_) {
                spin_target_ = pc_;
                spin_branch_ = cur_pc_;
                spin_ok_ = is_spin_loop(pc_, cur_pc_);
            }
            spin = spin_ok_;
        }
        if ((spin || pc_ == idle_pc_) && !irq_line_ && !dec_pending_) {
            // Idle loop: skip ahead to the end of the slice (or the next
            // decrementer exception).
            uint64_t skip = uint64_t(budget_ - done);
            if (dec_fire_cycle_ != ~0ull && dec_fire_cycle_ > total_cycles_)
                skip = std::min<uint64_t>(skip, dec_fire_cycle_ - total_cycles_);
            total_cycles_ += skip;
            done += int(skip);
        }
    }
    return done;
}

void Ppc603::set_fs(int n, double d) { set_fd(n, double(float(d))); }

void Ppc603::set_fprf(double d) {
    uint32_t c;
    if (std::isnan(d)) c = 0x11;
    else if (std::isinf(d)) c = d > 0 ? 0x05 : 0x09;
    else if (d == 0) c = std::signbit(d) ? 0x12 : 0x02;
    else if (std::fpclassify(d) == FP_SUBNORMAL) c = d > 0 ? 0x14 : 0x18;
    else c = d > 0 ? 0x04 : 0x08;
    fpscr_ = (fpscr_ & ~0x1f000u) | (c << 12);
}

#define RD ((op >> 21) & 31)
#define RS RD
#define RA ((op >> 16) & 31)
#define RB ((op >> 11) & 31)
#define RC ((op >> 6) & 31)
#define SIMM (int32_t(int16_t(op & 0xffff)))
#define UIMM (op & 0xffff)
#define RCBIT (op & 1)
#define OEBIT ((op >> 10) & 1)
#define EA_D (uint32_t((RA ? r_[RA] : 0) + uint32_t(SIMM)))
#define EA_X (uint32_t((RA ? r_[RA] : 0) + r_[RB]))

void Ppc603::exec(uint32_t op) {
    switch (op >> 26) {
        case 3: {  // twi
            const int32_t a = int32_t(r_[RA]), b = SIMM;
            const uint32_t to = RD;
            if (((to & 16) && a < b) || ((to & 8) && a > b) || ((to & 4) && a == b) ||
                ((to & 2) && uint32_t(a) < uint32_t(b)) || ((to & 1) && uint32_t(a) > uint32_t(b)))
                exception(0x700, cur_pc_, 0x20000);
            break;
        }
        case 7: r_[RD] = uint32_t(int32_t(r_[RA]) * SIMM); break;  // mulli
        case 8: {  // subfic
            const uint32_t a = r_[RA], b = uint32_t(SIMM);
            r_[RD] = b - a;
            xer_ca_ = ((uint64_t(~a) + b + 1) >> 32) != 0;
            break;
        }
        case 10: {  // cmpli
            const uint32_t a = r_[RA], b = UIMM;
            set_cr_field((op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (xer_so_ ? 1 : 0));
            break;
        }
        case 11: {  // cmpi
            const int32_t a = int32_t(r_[RA]), b = SIMM;
            set_cr_field((op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (xer_so_ ? 1 : 0));
            break;
        }
        case 12: case 13: {  // addic, addic.
            const uint32_t a = r_[RA], b = uint32_t(SIMM);
            const uint32_t r = a + b;
            xer_ca_ = r < a;
            r_[RD] = r;
            if (op >> 26 == 13) update_cr0(r);
            break;
        }
        case 14: r_[RD] = (RA ? r_[RA] : 0) + uint32_t(SIMM); break;           // addi
        case 15: r_[RD] = (RA ? r_[RA] : 0) + (uint32_t(UIMM) << 16); break;   // addis
        case 16: {  // bc
            const uint32_t bo = RD, bi = RA;
            if (!(bo & 4)) --ctr_;
            const bool ctr_ok = (bo & 4) || ((ctr_ != 0) != ((bo & 2) != 0));
            const bool cond_ok = (bo & 16) || (((cr_ >> (31 - bi)) & 1) == ((bo >> 3) & 1));
            const uint32_t target = uint32_t(int32_t(int16_t(op & 0xfffc))) + ((op & 2) ? 0 : cur_pc_);
            if (op & 1) lr_ = cur_pc_ + 4;
            if (ctr_ok && cond_ok) pc_ = target;
            break;
        }
        case 17:  // sc
            exception(0xc00, pc_);
            break;
        case 18: {  // b
            int32_t li = int32_t(op & 0x03fffffc);
            if (li & 0x02000000) li -= 0x04000000;
            if (op & 1) lr_ = cur_pc_ + 4;
            pc_ = uint32_t(li) + ((op & 2) ? 0 : cur_pc_);
            break;
        }
        case 19: exec19(op); break;
        case 20: {  // rlwimi
            const uint32_t m = mask((op >> 6) & 31, (op >> 1) & 31);
            r_[RA] = (rotl(r_[RS], RB) & m) | (r_[RA] & ~m);
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 21: {  // rlwinm
            r_[RA] = rotl(r_[RS], RB) & mask((op >> 6) & 31, (op >> 1) & 31);
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 23: {  // rlwnm
            r_[RA] = rotl(r_[RS], int(r_[RB] & 31)) & mask((op >> 6) & 31, (op >> 1) & 31);
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 24: r_[RA] = r_[RS] | UIMM; break;               // ori
        case 25: r_[RA] = r_[RS] | (UIMM << 16); break;       // oris
        case 26: r_[RA] = r_[RS] ^ UIMM; break;               // xori
        case 27: r_[RA] = r_[RS] ^ (UIMM << 16); break;       // xoris
        case 28: r_[RA] = r_[RS] & UIMM; update_cr0(r_[RA]); break;          // andi.
        case 29: r_[RA] = r_[RS] & (UIMM << 16); update_cr0(r_[RA]); break;  // andis.
        case 31: exec31(op); break;
        case 32: r_[RD] = rd32(EA_D); break;  // lwz
        case 33: { const uint32_t ea = EA_D; r_[RD] = rd32(ea); r_[RA] = ea; break; }  // lwzu
        case 34: r_[RD] = rd8(EA_D); break;   // lbz
        case 35: { const uint32_t ea = EA_D; r_[RD] = rd8(ea); r_[RA] = ea; break; }
        case 36: wr32(EA_D, r_[RS]); break;   // stw
        case 37: { const uint32_t ea = EA_D; wr32(ea, r_[RS]); r_[RA] = ea; break; }
        case 38: wr8(EA_D, uint8_t(r_[RS])); break;  // stb
        case 39: { const uint32_t ea = EA_D; wr8(ea, uint8_t(r_[RS])); r_[RA] = ea; break; }
        case 40: r_[RD] = rd16(EA_D); break;  // lhz
        case 41: { const uint32_t ea = EA_D; r_[RD] = rd16(ea); r_[RA] = ea; break; }
        case 42: r_[RD] = uint32_t(int32_t(int16_t(rd16(EA_D)))); break;  // lha
        case 43: { const uint32_t ea = EA_D; r_[RD] = uint32_t(int32_t(int16_t(rd16(ea)))); r_[RA] = ea; break; }
        case 44: wr16(EA_D, uint16_t(r_[RS])); break;  // sth
        case 45: { const uint32_t ea = EA_D; wr16(ea, uint16_t(r_[RS])); r_[RA] = ea; break; }
        case 46: {  // lmw
            uint32_t ea = EA_D;
            for (int r = int(RD); r < 32; ++r, ea += 4) r_[size_t(r)] = rd32(ea);
            break;
        }
        case 47: {  // stmw
            uint32_t ea = EA_D;
            for (int r = int(RS); r < 32; ++r, ea += 4) wr32(ea, r_[size_t(r)]);
            break;
        }
        case 48: case 49: {  // lfs, lfsu
            if (!fp_available()) break;
            const uint32_t ea = EA_D;
            const uint32_t bits = rd32(ea);
            float f;
            std::memcpy(&f, &bits, 4);
            set_fd(RD, double(f));
            if (op >> 26 == 49) r_[RA] = ea;
            break;
        }
        case 50: case 51: {  // lfd, lfdu
            if (!fp_available()) break;
            const uint32_t ea = EA_D;
            f_[RD] = rd64(ea);
            if (op >> 26 == 51) r_[RA] = ea;
            break;
        }
        case 52: case 53: {  // stfs, stfsu
            if (!fp_available()) break;
            const uint32_t ea = EA_D;
            const float f = float(fd(RS));
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            wr32(ea, bits);
            if (op >> 26 == 53) r_[RA] = ea;
            break;
        }
        case 54: case 55: {  // stfd, stfdu
            if (!fp_available()) break;
            const uint32_t ea = EA_D;
            wr64(ea, f_[RS]);
            if (op >> 26 == 55) r_[RA] = ea;
            break;
        }
        case 59: exec59(op); break;
        case 63: exec63(op); break;
        default: illegal(op); break;
    }
}

void Ppc603::exec19(uint32_t op) {
    const uint32_t xo = (op >> 1) & 0x3ff;
    auto crbit = [this](int b) { return (cr_ >> (31 - b)) & 1; };
    auto setbit = [this](int b, uint32_t v) {
        const uint32_t m = 1u << (31 - b);
        cr_ = v ? (cr_ | m) : (cr_ & ~m);
    };
    const int d = int(RD), a = int(RA), b = int(RB);
    switch (xo) {
        case 0: set_cr_field((op >> 23) & 7, (cr_ >> ((7 - ((op >> 18) & 7)) * 4)) & 0xf); break;  // mcrf
        case 16: case 528: {  // bclr, bcctr
            const uint32_t bo = RD, bi = RA;
            bool ctr_ok = true;
            if (xo == 16) {
                if (!(bo & 4)) --ctr_;
                ctr_ok = (bo & 4) || ((ctr_ != 0) != ((bo & 2) != 0));
            }
            const bool cond_ok = (bo & 16) || (((cr_ >> (31 - bi)) & 1) == ((bo >> 3) & 1));
            const uint32_t target = (xo == 16 ? lr_ : ctr_) & ~3u;
            if (op & 1) lr_ = cur_pc_ + 4;
            if (ctr_ok && cond_ok) pc_ = target;
            break;
        }
        case 33: setbit(d, !(crbit(a) | crbit(b))); break;   // crnor
        case 129: setbit(d, crbit(a) & !crbit(b)); break;    // crandc
        case 193: setbit(d, crbit(a) ^ crbit(b)); break;     // crxor
        case 225: setbit(d, !(crbit(a) & crbit(b))); break;  // crnand
        case 257: setbit(d, crbit(a) & crbit(b)); break;     // crand
        case 289: setbit(d, !(crbit(a) ^ crbit(b))); break;  // creqv
        case 417: setbit(d, crbit(a) | !crbit(b)); break;    // crorc
        case 449: setbit(d, crbit(a) | crbit(b)); break;     // cror
        case 50:  // rfi
            msr_ = (msr_ & ~0x87c0ff73u) | (srr1_ & 0x87c0ff73u);
            pc_ = srr0_ & ~3u;
            break;
        case 150: break;  // isync
        default: illegal(op); break;
    }
}

void Ppc603::exec31(uint32_t op) {
    const uint32_t xo9 = (op >> 1) & 0x1ff;
    // Arithmetic with OE
    switch (xo9) {
        case 8: case 10: case 11: case 40: case 75: case 104: case 136: case 138:
        case 200: case 202: case 232: case 234: case 235: case 266: case 459: case 491: {
            const uint32_t a = r_[RA], b = r_[RB];
            uint32_t r = 0;
            bool ov = false;
            auto add_ov = [](uint32_t x, uint32_t y, uint32_t res) { return ((x ^ res) & (y ^ res) & 0x80000000u) != 0; };
            switch (xo9) {
                case 266: r = a + b; ov = add_ov(a, b, r); break;  // add
                case 10: {  // addc
                    r = a + b;
                    xer_ca_ = r < a;
                    ov = add_ov(a, b, r);
                    break;
                }
                case 138: {  // adde
                    const uint64_t s = uint64_t(a) + b + (xer_ca_ ? 1 : 0);
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(a, b, r);
                    break;
                }
                case 234: {  // addme
                    const uint64_t s = uint64_t(a) + 0xffffffffull + (xer_ca_ ? 1 : 0);
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(a, 0xffffffffu, r);
                    break;
                }
                case 202: {  // addze
                    const uint64_t s = uint64_t(a) + (xer_ca_ ? 1 : 0);
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(a, 0, r);
                    break;
                }
                case 40: r = b - a; ov = add_ov(~a, b, r); break;  // subf
                case 8: {  // subfc
                    const uint64_t s = uint64_t(~a) + b + 1;
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(~a, b, r);
                    break;
                }
                case 136: {  // subfe
                    const uint64_t s = uint64_t(~a) + b + (xer_ca_ ? 1 : 0);
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(~a, b, r);
                    break;
                }
                case 232: {  // subfme
                    const uint64_t s = uint64_t(~a) + 0xffffffffull + (xer_ca_ ? 1 : 0);
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(~a, 0xffffffffu, r);
                    break;
                }
                case 200: {  // subfze
                    const uint64_t s = uint64_t(~a) + (xer_ca_ ? 1 : 0);
                    r = uint32_t(s);
                    xer_ca_ = s >> 32;
                    ov = add_ov(~a, 0, r);
                    break;
                }
                case 104: r = uint32_t(-int64_t(int32_t(a))); ov = a == 0x80000000u; break;  // neg
                case 235: {  // mullw
                    const int64_t p = int64_t(int32_t(a)) * int64_t(int32_t(b));
                    r = uint32_t(p);
                    ov = p != int64_t(int32_t(r));
                    break;
                }
                case 75: r = uint32_t((int64_t(int32_t(a)) * int64_t(int32_t(b))) >> 32); break;  // mulhw
                case 11: r = uint32_t((uint64_t(a) * uint64_t(b)) >> 32); break;                   // mulhwu
                case 491:  // divw
                    if (b == 0 || (a == 0x80000000u && b == 0xffffffffu)) {
                        r = (int32_t(a) < 0 && b == 0) ? 0xffffffffu : 0;
                        ov = true;
                    } else {
                        r = uint32_t(int32_t(a) / int32_t(b));
                    }
                    break;
                case 459:  // divwu
                    if (b == 0) {
                        r = 0;
                        ov = true;
                    } else {
                        r = a / b;
                    }
                    break;
            }
            r_[RD] = r;
            if (OEBIT && xo9 != 75 && xo9 != 11) {
                xer_ov_ = ov;
                if (ov) xer_so_ = true;
            }
            if (RCBIT) update_cr0(r);
            return;
        }
        default: break;
    }

    const uint32_t xo = (op >> 1) & 0x3ff;
    switch (xo) {
        case 0: {  // cmp
            const int32_t a = int32_t(r_[RA]), b = int32_t(r_[RB]);
            set_cr_field((op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (xer_so_ ? 1 : 0));
            break;
        }
        case 32: {  // cmpl
            const uint32_t a = r_[RA], b = r_[RB];
            set_cr_field((op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (xer_so_ ? 1 : 0));
            break;
        }
        case 4: {  // tw
            const int32_t a = int32_t(r_[RA]), b = int32_t(r_[RB]);
            const uint32_t to = RD;
            if (((to & 16) && a < b) || ((to & 8) && a > b) || ((to & 4) && a == b) ||
                ((to & 2) && uint32_t(a) < uint32_t(b)) || ((to & 1) && uint32_t(a) > uint32_t(b)))
                exception(0x700, cur_pc_, 0x20000);
            break;
        }
        case 19: r_[RD] = cr_; break;  // mfcr
        case 20:  // lwarx
            reserve_ = true;
            reserve_addr_ = EA_X;
            r_[RD] = rd32(reserve_addr_);
            break;
        case 150:  // stwcx.
            if (reserve_) {
                wr32(EA_X, r_[RS]);
                set_cr_field(0, 2 | (xer_so_ ? 1 : 0));
            } else {
                set_cr_field(0, xer_so_ ? 1 : 0);
            }
            reserve_ = false;
            break;
        case 23: r_[RD] = rd32(EA_X); break;                                       // lwzx
        case 55: { const uint32_t ea = EA_X; r_[RD] = rd32(ea); r_[RA] = ea; break; }  // lwzux
        case 87: r_[RD] = rd8(EA_X); break;                                        // lbzx
        case 119: { const uint32_t ea = EA_X; r_[RD] = rd8(ea); r_[RA] = ea; break; }
        case 279: r_[RD] = rd16(EA_X); break;                                      // lhzx
        case 311: { const uint32_t ea = EA_X; r_[RD] = rd16(ea); r_[RA] = ea; break; }
        case 343: r_[RD] = uint32_t(int32_t(int16_t(rd16(EA_X)))); break;          // lhax
        case 375: { const uint32_t ea = EA_X; r_[RD] = uint32_t(int32_t(int16_t(rd16(ea)))); r_[RA] = ea; break; }
        case 151: wr32(EA_X, r_[RS]); break;                                       // stwx
        case 183: { const uint32_t ea = EA_X; wr32(ea, r_[RS]); r_[RA] = ea; break; }
        case 215: wr8(EA_X, uint8_t(r_[RS])); break;                               // stbx
        case 247: { const uint32_t ea = EA_X; wr8(ea, uint8_t(r_[RS])); r_[RA] = ea; break; }
        case 407: wr16(EA_X, uint16_t(r_[RS])); break;                             // sthx
        case 439: { const uint32_t ea = EA_X; wr16(ea, uint16_t(r_[RS])); r_[RA] = ea; break; }
        case 534: r_[RD] = bswap32(rd32(EA_X)); break;                             // lwbrx
        case 790: r_[RD] = bswap16(rd16(EA_X)); break;                             // lhbrx
        case 662: wr32(EA_X, bswap32(r_[RS])); break;                              // stwbrx
        case 918: wr16(EA_X, bswap16(uint16_t(r_[RS]))); break;                    // sthbrx
        case 24: {  // slw
            const uint32_t n = r_[RB] & 63;
            r_[RA] = n >= 32 ? 0 : r_[RS] << n;
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 536: {  // srw
            const uint32_t n = r_[RB] & 63;
            r_[RA] = n >= 32 ? 0 : r_[RS] >> n;
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 792: {  // sraw
            const uint32_t n = r_[RB] & 63;
            const int32_t s = int32_t(r_[RS]);
            if (n >= 32) {
                r_[RA] = s < 0 ? 0xffffffffu : 0;
                xer_ca_ = s < 0;
            } else {
                r_[RA] = uint32_t(s >> n);
                xer_ca_ = s < 0 && n && (uint32_t(s) & ((1u << n) - 1));
            }
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 824: {  // srawi
            const uint32_t n = RB;
            const int32_t s = int32_t(r_[RS]);
            r_[RA] = uint32_t(s >> n);
            xer_ca_ = s < 0 && n && (uint32_t(s) & ((1u << n) - 1));
            if (RCBIT) update_cr0(r_[RA]);
            break;
        }
        case 26: r_[RA] = r_[RS] ? uint32_t(__builtin_clz(r_[RS])) : 32; if (RCBIT) update_cr0(r_[RA]); break;  // cntlzw
        case 28: r_[RA] = r_[RS] & r_[RB]; if (RCBIT) update_cr0(r_[RA]); break;      // and
        case 60: r_[RA] = r_[RS] & ~r_[RB]; if (RCBIT) update_cr0(r_[RA]); break;     // andc
        case 124: r_[RA] = ~(r_[RS] | r_[RB]); if (RCBIT) update_cr0(r_[RA]); break;  // nor
        case 284: r_[RA] = ~(r_[RS] ^ r_[RB]); if (RCBIT) update_cr0(r_[RA]); break;  // eqv
        case 316: r_[RA] = r_[RS] ^ r_[RB]; if (RCBIT) update_cr0(r_[RA]); break;     // xor
        case 412: r_[RA] = r_[RS] | ~r_[RB]; if (RCBIT) update_cr0(r_[RA]); break;    // orc
        case 444: r_[RA] = r_[RS] | r_[RB]; if (RCBIT) update_cr0(r_[RA]); break;     // or
        case 476: r_[RA] = ~(r_[RS] & r_[RB]); if (RCBIT) update_cr0(r_[RA]); break;  // nand
        case 922: r_[RA] = uint32_t(int32_t(int16_t(r_[RS]))); if (RCBIT) update_cr0(r_[RA]); break;  // extsh
        case 954: r_[RA] = uint32_t(int32_t(int8_t(r_[RS]))); if (RCBIT) update_cr0(r_[RA]); break;   // extsb
        case 83: r_[RD] = msr_; break;                                              // mfmsr
        case 146: msr_ = r_[RS]; break;                                             // mtmsr
        case 144: {  // mtcrf
            const uint32_t crm = (op >> 12) & 0xff;
            uint32_t m = 0;
            for (int i = 0; i < 8; ++i)
                if (crm & (0x80 >> i)) m |= 0xf0000000u >> (i * 4);
            cr_ = (cr_ & ~m) | (r_[RS] & m);
            break;
        }
        case 512: {  // mcrxr
            set_cr_field((op >> 23) & 7, (xer_so_ ? 8 : 0) | (xer_ov_ ? 4 : 0) | (xer_ca_ ? 2 : 0));
            xer_so_ = xer_ov_ = xer_ca_ = false;
            break;
        }
        case 339: r_[RD] = read_spr(int(((op >> 16) & 31) | (((op >> 11) & 31) << 5))); break;  // mfspr
        case 371: r_[RD] = read_spr(int(((op >> 16) & 31) | (((op >> 11) & 31) << 5))); break;  // mftb
        case 467: write_spr(int(((op >> 16) & 31) | (((op >> 11) & 31) << 5)), r_[RS]); break;  // mtspr
        case 595: r_[RD] = sr_[(op >> 16) & 15]; break;                               // mfsr
        case 659: r_[RD] = sr_[r_[RB] >> 28]; break;                                  // mfsrin
        case 210: sr_[(op >> 16) & 15] = r_[RS]; break;                               // mtsr
        case 242: sr_[r_[RB] >> 28] = r_[RS]; break;                                  // mtsrin
        case 1014: {  // dcbz
            const uint32_t ea = EA_X & ~31u;
            for (int i = 0; i < 32; i += 8) wr64(ea + uint32_t(i), 0);
            break;
        }
        case 54: case 86: case 246: case 278: case 470: case 982:  // dcbst dcbf dcbtst dcbt dcbi icbi
        case 306: case 370: case 566: case 598: case 854:          // tlbie tlbia tlbsync sync eieio
        case 978: case 1010:                                       // tlbld tlbli
            break;
        case 310: r_[RD] = rd32(EA_X); break;   // eciwx
        case 438: wr32(EA_X, r_[RS]); break;    // ecowx
        case 597: case 533: {  // lswi, lswx
            uint32_t ea = xo == 597 ? (RA ? r_[RA] : 0) : EA_X;
            int n = xo == 597 ? (RB ? int(RB) : 32) : int(xer_bc_ & 0x7f);
            int r = int(RD) - 1, i = 4;
            while (n > 0) {
                if (i == 4) {
                    r = (r + 1) & 31;
                    r_[size_t(r)] = 0;
                    i = 0;
                }
                r_[size_t(r)] |= uint32_t(rd8(ea)) << (24 - i * 8);
                ++ea;
                ++i;
                --n;
            }
            break;
        }
        case 725: case 661: {  // stswi, stswx
            uint32_t ea = xo == 725 ? (RA ? r_[RA] : 0) : EA_X;
            int n = xo == 725 ? (RB ? int(RB) : 32) : int(xer_bc_ & 0x7f);
            int r = int(RS) - 1, i = 4;
            while (n > 0) {
                if (i == 4) {
                    r = (r + 1) & 31;
                    i = 0;
                }
                wr8(ea, uint8_t(r_[size_t(r)] >> (24 - i * 8)));
                ++ea;
                ++i;
                --n;
            }
            break;
        }
        case 535: case 567: {  // lfsx, lfsux
            if (!fp_available()) break;
            const uint32_t ea = EA_X;
            const uint32_t bits = rd32(ea);
            float f;
            std::memcpy(&f, &bits, 4);
            set_fd(RD, double(f));
            if (xo == 567) r_[RA] = ea;
            break;
        }
        case 599: case 631: {  // lfdx, lfdux
            if (!fp_available()) break;
            const uint32_t ea = EA_X;
            f_[RD] = rd64(ea);
            if (xo == 631) r_[RA] = ea;
            break;
        }
        case 663: case 695: {  // stfsx, stfsux
            if (!fp_available()) break;
            const uint32_t ea = EA_X;
            const float f = float(fd(RS));
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            wr32(ea, bits);
            if (xo == 695) r_[RA] = ea;
            break;
        }
        case 727: case 759: {  // stfdx, stfdux
            if (!fp_available()) break;
            const uint32_t ea = EA_X;
            wr64(ea, f_[RS]);
            if (xo == 759) r_[RA] = ea;
            break;
        }
        case 983:  // stfiwx
            if (!fp_available()) break;
            wr32(EA_X, uint32_t(f_[RS]));
            break;
        default: illegal(op); break;
    }
}

void Ppc603::exec59(uint32_t op) {
    if (!fp_available()) return;
    const double a = fd(RA), b = fd(RB), c = fd(RC);
    double r;
    switch ((op >> 1) & 31) {
        case 18: r = a / b; break;                 // fdivs
        case 20: r = a - b; break;                 // fsubs
        case 21: r = a + b; break;                 // fadds
        case 22: r = std::sqrt(b); break;          // fsqrts
        case 24: r = 1.0 / b; break;               // fres
        case 25: r = a * c; break;                 // fmuls
        case 28: r = std::fma(a, c, -b); break;    // fmsubs
        case 29: r = std::fma(a, c, b); break;     // fmadds
        case 30: r = -std::fma(a, c, -b); break;   // fnmsubs
        case 31: r = -std::fma(a, c, b); break;    // fnmadds
        default: illegal(op); return;
    }
    set_fs(RD, r);
    set_fprf(fd(RD));
    if (RCBIT) update_cr1();
}

void Ppc603::exec63(uint32_t op) {
    if (!fp_available()) return;
    const uint32_t xo5 = (op >> 1) & 31;
    if (xo5 >= 16) {
        const double a = fd(RA), b = fd(RB), c = fd(RC);
        double r;
        switch (xo5) {
            case 18: r = a / b; break;                         // fdiv
            case 20: r = a - b; break;                         // fsub
            case 21: r = a + b; break;                         // fadd
            case 22: r = std::sqrt(b); break;                  // fsqrt
            case 23: r = (a >= 0.0) ? c : b; break;            // fsel (NaN selects b)
            case 25: r = a * c; break;                         // fmul
            case 26: r = 1.0 / std::sqrt(b); break;            // frsqrte
            case 28: r = std::fma(a, c, -b); break;            // fmsub
            case 29: r = std::fma(a, c, b); break;             // fmadd
            case 30: r = -std::fma(a, c, -b); break;           // fnmsub
            case 31: r = -std::fma(a, c, b); break;            // fnmadd
            default: illegal(op); return;
        }
        set_fd(RD, r);
        if (xo5 != 23) set_fprf(r);
        if (RCBIT) update_cr1();
        return;
    }
    const uint32_t xo = (op >> 1) & 0x3ff;
    switch (xo) {
        case 0: case 32: {  // fcmpu, fcmpo
            const double a = fd(RA), b = fd(RB);
            uint32_t c;
            if (std::isnan(a) || std::isnan(b)) c = 1;
            else if (a < b) c = 8;
            else if (a > b) c = 4;
            else c = 2;
            set_cr_field((op >> 23) & 7, c);
            fpscr_ = (fpscr_ & ~0xf000u) | (c << 12);
            break;
        }
        case 12:  // frsp
            set_fs(RD, fd(RB));
            set_fprf(fd(RD));
            if (RCBIT) update_cr1();
            break;
        case 14: case 15: {  // fctiw, fctiwz
            const double b = fd(RB);
            int32_t v;
            if (std::isnan(b)) {
                v = std::numeric_limits<int32_t>::min();
            } else {
                double t;
                const int mode = xo == 15 ? 1 : int(fpscr_ & 3);
                switch (mode) {
                    case 0: t = std::nearbyint(b); break;
                    case 1: t = std::trunc(b); break;
                    case 2: t = std::ceil(b); break;
                    default: t = std::floor(b); break;
                }
                if (t > 2147483647.0) v = std::numeric_limits<int32_t>::max();
                else if (t < -2147483648.0) v = std::numeric_limits<int32_t>::min();
                else v = int32_t(t);
            }
            f_[RD] = 0xfff8000000000000ull | uint32_t(v);
            if (RCBIT) update_cr1();
            break;
        }
        case 38: fpscr_ |= 0x80000000u >> RD; if (RCBIT) update_cr1(); break;    // mtfsb1
        case 70: fpscr_ &= ~(0x80000000u >> RD); if (RCBIT) update_cr1(); break; // mtfsb0
        case 40: f_[RD] = f_[RB] ^ 0x8000000000000000ull; if (RCBIT) update_cr1(); break;  // fneg
        case 72: f_[RD] = f_[RB]; if (RCBIT) update_cr1(); break;                          // fmr
        case 136: f_[RD] = f_[RB] | 0x8000000000000000ull; if (RCBIT) update_cr1(); break; // fnabs
        case 264: f_[RD] = f_[RB] & ~0x8000000000000000ull; if (RCBIT) update_cr1(); break;// fabs
        case 64: {  // mcrfs
            const int s = int((op >> 18) & 7);
            set_cr_field((op >> 23) & 7, (fpscr_ >> ((7 - s) * 4)) & 0xf);
            fpscr_ &= ~((0xfu << ((7 - s) * 4)) & 0x9ff80700u);
            break;
        }
        case 134: {  // mtfsfi
            const int field = int((op >> 23) & 7);
            const uint32_t imm = (op >> 12) & 0xf;
            const int sh = (7 - field) * 4;
            fpscr_ = (fpscr_ & ~(0xfu << sh)) | (imm << sh);
            if (RCBIT) update_cr1();
            break;
        }
        case 583: f_[RD] = 0xfff8000000000000ull | fpscr_; if (RCBIT) update_cr1(); break;  // mffs
        case 711: {  // mtfsf
            const uint32_t fm = (op >> 17) & 0xff;
            uint32_t m = 0;
            for (int i = 0; i < 8; ++i)
                if (fm & (0x80 >> i)) m |= 0xf0000000u >> (i * 4);
            fpscr_ = (fpscr_ & ~m) | (uint32_t(f_[RB]) & m);
            if (RCBIT) update_cr1();
            break;
        }
        default: illegal(op); break;
    }
}

}  // namespace dsp
