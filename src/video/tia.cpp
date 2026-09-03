#include "video/tia.h"

#include <algorithm>

namespace dsp {
namespace {

// Classic NTSC TIA palette (16 hues × 8 luminances). Bit 0 of COLUxx is ignored.
constexpr uint32_t kNtsc[128] = {
    0xFF000000, 0xFF4A4A4A, 0xFF6F6F6F, 0xFF8E8E8E, 0xFFAAAAAA, 0xFFC0C0C0, 0xFFD6D6D6, 0xFFECECEC,
    0xFF484800, 0xFF69690F, 0xFF86861D, 0xFFA2A22A, 0xFFBBBB35, 0xFFD2D240, 0xFFE8E84A, 0xFFFCFC54,
    0xFF7C2C00, 0xFF904811, 0xFFA26221, 0xFFB47A30, 0xFFC3903D, 0xFFD2A44A, 0xFFDFB755, 0xFFECC860,
    0xFF901C00, 0xFFA23A14, 0xFFB55326, 0xFFC66A37, 0xFFD67E46, 0xFFE49052, 0xFFF0A05E, 0xFFFCAF69,
    0xFF940000, 0xFFA71A1A, 0xFFB83232, 0xFFC84848, 0xFFD65C5C, 0xFFE46F6F, 0xFFF08080, 0xFFFC9090,
    0xFF840064, 0xFF961A74, 0xFFA83084, 0xFFB84494, 0xFFC656A4, 0xFFD266B4, 0xFFDC74C4, 0xFFE680D4,
    0xFF700070, 0xFF801880, 0xFF8E2C8E, 0xFF9C409C, 0xFFAA52AA, 0xFFB662B6, 0xFFC270C2, 0xFFCC7CCC,
    0xFF48007C, 0xFF5A148E, 0xFF6A269E, 0xFF7A38AE, 0xFF8848BE, 0xFF9658CE, 0xFFA266DE, 0xFFAE72EE,
    0xFF140084, 0xFF261696, 0xFF3828A6, 0xFF4838B6, 0xFF5848C6, 0xFF6858D6, 0xFF7666E6, 0xFF8272F6,
    0xFF000088, 0xFF12129A, 0xFF2424AA, 0xFF3434BA, 0xFF4444CA, 0xFF5454DA, 0xFF6262EA, 0xFF7070FA,
    0xFF00187C, 0xFF122E8E, 0xFF24429E, 0xFF3454AE, 0xFF4464BE, 0xFF5474CE, 0xFF6282DE, 0xFF7090EE,
    0xFF002C5C, 0xFF12426E, 0xFF24567E, 0xFF34688E, 0xFF44789E, 0xFF5488AE, 0xFF6296BE, 0xFF70A4CE,
    0xFF003C2C, 0xFF12523E, 0xFF24664E, 0xFF34785E, 0xFF44886E, 0xFF54987E, 0xFF62A68E, 0xFF70B49E,
    0xFF003C00, 0xFF125214, 0xFF246628, 0xFF34783C, 0xFF448850, 0xFF549864, 0xFF62A678, 0xFF70B48C,
    0xFF143800, 0xFF264E0C, 0xFF38621C, 0xFF48742C, 0xFF58843C, 0xFF68944C, 0xFF76A25C, 0xFF84B06C,
    0xFF2C3000, 0xFF3E4610, 0xFF505A20, 0xFF606C30, 0xFF707C40, 0xFF808C50, 0xFF8E9A60, 0xFF9CA870,
};

int wrap_clock(int clock) {
    clock %= Tia::kColorClocksPerLine;
    if (clock < 0) clock += Tia::kColorClocksPerLine;
    return clock;
}

}  // namespace

Tia::Tia() { reset(); }

void Tia::reset() {
    wsync_ = vsync_ = vblank_ = false;
    dump_ports_ = latch_inputs_ = false;
    nusiz_[0] = nusiz_[1] = 0;
    colup_[0] = colup_[1] = 0;
    colupf_ = colubk_ = ctrlpf_ = 0;
    refp_[0] = refp_[1] = 0;
    pf0_ = pf1_ = pf2_ = 0;
    grp_[0] = grp_[1] = 0;
    grp_delay_[0] = grp_delay_[1] = 0;
    enam_[0] = enam_[1] = 0;
    enabl_ = enabl_delay_ = 0;
    hmm_.fill(0);
    vdelp_[0] = vdelp_[1] = false;
    vdelbl_ = false;
    resmp_[0] = resmp_[1] = false;
    pos_.fill(0);
    hclock_ = 0;
    drawn_clock_ = 0;
    line_.fill(0xFF000000);
    cx_.fill(0);
    inpt4_ = inpt5_ = false;
    inpt4_latched_ = inpt5_latched_ = false;
    for (Channel& ch : ch_) ch = Channel{};
    sample_ = 0;
    audio_phase_ = 0;
}

uint32_t Tia::ntsc_color(uint8_t colu) { return kNtsc[(colu >> 1) & 0x7f]; }

void Tia::set_inpt4(bool pressed) {
    inpt4_ = pressed;
    if (pressed) inpt4_latched_ = true;
}

void Tia::set_inpt5(bool pressed) {
    inpt5_ = pressed;
    if (pressed) inpt5_latched_ = true;
}

void Tia::set_hclock(int color_clocks) { hclock_ = color_clocks; }

void Tia::add_cpu_cycles(int cycles) {
    if (cycles > 0) hclock_ += cycles * 3;
}

void Tia::begin_line() {
    hclock_ = 0;
    drawn_clock_ = 0;
    line_.fill(0xFF000000);
}

void Tia::flush() {
    int target = hclock_;
    if (target > kColorClocksPerLine) target = kColorClocksPerLine;
    if (target <= drawn_clock_) return;
    for (int clock = drawn_clock_; clock < target; clock++) {
        if (clock < kHblankClocks) continue;
        const int x = clock - kHblankClocks;
        if (x >= kScreenWidth) break;
        line_[size_t(x)] = sample_pixel(clock);
    }
    drawn_clock_ = target;
}

int Tia::player_scale(uint8_t nusiz) {
    switch (nusiz & 7) {
        case 5: return 2;
        case 7: return 4;
        default: return 1;
    }
}

int Tia::copy_count(uint8_t nusiz) {
    switch (nusiz & 7) {
        case 1:
        case 2:
        case 4: return 2;
        case 3:
        case 6: return 3;
        default: return 1;
    }
}

int Tia::copy_offset(uint8_t nusiz, int copy) {
    if (copy == 0) return 0;
    switch (nusiz & 7) {
        case 1: return 16;
        case 2: return 32;
        case 3: return copy == 1 ? 16 : 32;
        case 4: return 64;
        case 6: return copy == 1 ? 32 : 64;
        default: return 0;
    }
}

int Tia::missile_width(uint8_t nusiz) { return 1 << ((nusiz >> 4) & 3); }

void Tia::reset_object(int index) { pos_[index] = hclock_; }

void Tia::apply_hmove() {
    for (int i = 0; i < 5; i++) {
        const int delta = int8_t(hmm_[i]) >> 4;  // -8 .. +7
        pos_[i] = wrap_clock(pos_[i] - delta);
    }
}

uint8_t Tia::visible_grp(int which) const {
    return vdelp_[which] ? grp_delay_[which] : grp_[which];
}

bool Tia::missile_enabled(int which) const {
    return !resmp_[which] && (enam_[which] & 0x02) != 0;
}

bool Tia::ball_enabled() const {
    const uint8_t en = vdelbl_ ? enabl_delay_ : enabl_;
    return (en & 0x02) != 0;
}

int Tia::player_pixel(int clock, int which) const {
    const uint8_t grp = visible_grp(which);
    if (grp == 0) return 0;
    const uint8_t nusiz = nusiz_[which];
    const int scale = player_scale(nusiz);
    const int copies = copy_count(nusiz);
    const bool reflect = (refp_[which] & 0x08) != 0;
    for (int copy = 0; copy < copies; copy++) {
        const int rel = clock - (pos_[which] + kRespDelay + copy_offset(nusiz, copy));
        if (rel < 0 || rel >= 8 * scale) continue;
        int bit = rel / scale;
        if (reflect) bit = 7 - bit;
        if ((grp >> (7 - bit)) & 1) return 1;
    }
    return 0;
}

int Tia::missile_pixel(int clock, int which) const {
    if (!missile_enabled(which)) return 0;
    const uint8_t nusiz = nusiz_[which];
    const int width = missile_width(nusiz);
    const int copies = copy_count(nusiz);
    const int base = resmp_[which] ? pos_[which] + 4 * player_scale(nusiz)
                                   : pos_[2 + which];
    for (int copy = 0; copy < copies; copy++) {
        const int rel = clock - (base + kRespDelay + copy_offset(nusiz, copy));
        if (rel >= 0 && rel < width) return 1;
    }
    return 0;
}

int Tia::ball_pixel(int clock) const {
    if (!ball_enabled()) return 0;
    const int width = 1 << ((ctrlpf_ >> 4) & 3);
    const int rel = clock - (pos_[4] + kRespDelay);
    return (rel >= 0 && rel < width) ? 1 : 0;
}

int Tia::playfield_pixel(int x) const {
    int cell = x / 4;
    if (cell >= 20) {
        cell = (ctrlpf_ & 0x01) ? (39 - cell) : (cell - 20);
    }
    if (cell < 4) return (pf0_ >> (4 + cell)) & 1;
    if (cell < 12) return (pf1_ >> (7 - (cell - 4))) & 1;
    return (pf2_ >> (cell - 12)) & 1;
}

void Tia::write(uint8_t reg, uint8_t value) {
    const uint8_t r = uint8_t(reg & 0x3f);
    if (r != 0x02 && r < 0x15) flush();
    else if (r >= 0x1b && r <= 0x2c) flush();
    switch (r) {
        case 0x00: vsync_ = (value & 0x02) != 0; break;
        case 0x01:
            vblank_ = (value & 0x02) != 0;
            latch_inputs_ = (value & 0x40) != 0;
            dump_ports_ = (value & 0x80) != 0;
            if (!latch_inputs_) inpt4_latched_ = inpt5_latched_ = false;
            break;
        case 0x02: wsync_ = true; break;
        case 0x03:
            flush();
            hclock_ = 0;
            drawn_clock_ = 0;
            break;
        case 0x04: nusiz_[0] = value; break;
        case 0x05: nusiz_[1] = value; break;
        case 0x06: colup_[0] = value; break;
        case 0x07: colup_[1] = value; break;
        case 0x08: colupf_ = value; break;
        case 0x09: colubk_ = value; break;
        case 0x0a: ctrlpf_ = value; break;
        case 0x0b: refp_[0] = value; break;
        case 0x0c: refp_[1] = value; break;
        case 0x0d: pf0_ = value; break;
        case 0x0e: pf1_ = value; break;
        case 0x0f: pf2_ = value; break;
        case 0x10: reset_object(0); break;
        case 0x11: reset_object(1); break;
        case 0x12: reset_object(2); break;
        case 0x13: reset_object(3); break;
        case 0x14: reset_object(4); break;
        case 0x15: ch_[0].audc = value & 0x0f; break;
        case 0x16: ch_[1].audc = value & 0x0f; break;
        case 0x17: ch_[0].audf = value & 0x1f; break;
        case 0x18: ch_[1].audf = value & 0x1f; break;
        case 0x19: ch_[0].audv = value & 0x0f; break;
        case 0x1a: ch_[1].audv = value & 0x0f; break;
        case 0x1b:
            grp_[0] = value;
            grp_delay_[1] = grp_[1];
            break;
        case 0x1c:
            grp_[1] = value;
            grp_delay_[0] = grp_[0];
            enabl_delay_ = enabl_;
            break;
        case 0x1d: enam_[0] = value; break;
        case 0x1e: enam_[1] = value; break;
        case 0x1f: enabl_ = value; break;
        case 0x20: hmm_[0] = value; break;
        case 0x21: hmm_[1] = value; break;
        case 0x22: hmm_[2] = value; break;
        case 0x23: hmm_[3] = value; break;
        case 0x24: hmm_[4] = value; break;
        case 0x25: vdelp_[0] = (value & 0x01) != 0; break;
        case 0x26: vdelp_[1] = (value & 0x01) != 0; break;
        case 0x27: vdelbl_ = (value & 0x01) != 0; break;
        case 0x28: resmp_[0] = (value & 0x02) != 0; break;
        case 0x29: resmp_[1] = (value & 0x02) != 0; break;
        case 0x2a: apply_hmove(); break;
        case 0x2b: hmm_.fill(0); break;
        case 0x2c: cx_.fill(0); break;
        default: break;
    }
}

uint8_t Tia::read(uint8_t reg) const {
    reg &= 0x0f;
    if (reg <= 0x07) return cx_[reg];
    if (reg == 0x0c) return (latch_inputs_ ? inpt4_latched_ : inpt4_) ? 0x00 : 0x80;
    if (reg == 0x0d) return (latch_inputs_ ? inpt5_latched_ : inpt5_) ? 0x00 : 0x80;
    return dump_ports_ ? 0x00 : 0x80;
}

uint32_t Tia::sample_pixel(int clock) {
    if (blanked()) return 0xFF000000;

    const int x = clock - kHblankClocks;
    const uint32_t bk = ntsc_color(colubk_);
    const uint32_t pf_color = ntsc_color(colupf_);
    const uint32_t p0_color = ntsc_color(colup_[0]);
    const uint32_t p1_color = ntsc_color(colup_[1]);
    const bool pf_priority = (ctrlpf_ & 0x04) != 0;
    const bool score = (ctrlpf_ & 0x02) != 0;

    if (resmp_[0]) pos_[2] = pos_[0] + 4 * player_scale(nusiz_[0]);
    if (resmp_[1]) pos_[3] = pos_[1] + 4 * player_scale(nusiz_[1]);

    const int pf = playfield_pixel(x);
    const int p0 = player_pixel(clock, 0);
    const int p1 = player_pixel(clock, 1);
    const int m0 = missile_pixel(clock, 0);
    const int m1 = missile_pixel(clock, 1);
    const int bl = ball_pixel(clock);

    if (m0 && p0) cx_[0] |= 0x40;
    if (m0 && p1) cx_[0] |= 0x80;
    if (m1 && p1) cx_[1] |= 0x40;
    if (m1 && p0) cx_[1] |= 0x80;
    if (p0 && pf) cx_[2] |= 0x80;
    if (p0 && bl) cx_[2] |= 0x40;
    if (p1 && pf) cx_[3] |= 0x80;
    if (p1 && bl) cx_[3] |= 0x40;
    if (m0 && pf) cx_[4] |= 0x80;
    if (m0 && bl) cx_[4] |= 0x40;
    if (m1 && pf) cx_[5] |= 0x80;
    if (m1 && bl) cx_[5] |= 0x40;
    if (bl && pf) cx_[6] |= 0x80;
    if (p0 && p1) cx_[7] |= 0x80;
    if (m0 && m1) cx_[7] |= 0x40;

    const uint32_t scored = score ? (x < 80 ? p0_color : p1_color) : pf_color;
    if (pf_priority) {
        if (pf) return scored;
        if (bl) return pf_color;
        if (p0 || m0) return p0_color;
        if (p1 || m1) return p1_color;
    } else {
        if (p0 || m0) return p0_color;
        if (p1 || m1) return p1_color;
        if (pf) return scored;
        if (bl) return pf_color;
    }
    return bk;
}

void Tia::render_line(uint32_t* dest) {
    if (hclock_ < kColorClocksPerLine) hclock_ = kColorClocksPerLine;
    flush();
    std::copy(line_.begin(), line_.end(), dest);
}

void Tia::clock_channel(Channel& ch) {
    if (++ch.divider <= ch.audf) return;
    ch.divider = 0;

    const uint8_t bit5 = uint8_t(((ch.poly5 >> 2) ^ (ch.poly5 >> 4)) & 1);
    ch.poly5 = uint8_t(((ch.poly5 << 1) | bit5) & 0x1f);

    const uint8_t mode = ch.audc & 0x0f;
    switch (mode) {
        case 0x0:
        case 0xb:
            ch.bit = 1;
            return;
        case 0x4:
        case 0x5:
            ch.bit ^= 1;
            return;
        case 0x6:
        case 0xa:
            ch.bit = (ch.poly5 == 0x1f) ? 1 : 0;
            return;
        case 0xc:
        case 0xd:
            if (++ch.div6 >= 3) {
                ch.div6 = 0;
                ch.bit ^= 1;
            }
            return;
        case 0x8: {
            const uint8_t bit9 = uint8_t(((ch.poly9 >> 4) ^ (ch.poly9 >> 8)) & 1);
            ch.poly9 = uint16_t(((ch.poly9 << 1) | bit9) & 0x1ff);
            ch.bit = uint8_t(ch.poly9 & 1);
            return;
        }
        default:
            break;
    }

    bool clock4 = true;
    if (mode == 0x2 || mode == 0x3) clock4 = ch.poly5 == 0x1f;
    else if (mode == 0x7 || mode == 0x9 || mode == 0xf) clock4 = bit5 != 0;
    else if (mode == 0xe) {
        if (ch.poly5 == 0x1f) ch.bit ^= 1;
        return;
    }

    if (!clock4) return;
    const uint8_t bit4 = uint8_t(((ch.poly4 >> 3) ^ (ch.poly4 >> 2)) & 1);
    ch.poly4 = uint8_t(((ch.poly4 << 1) | bit4) & 0x0f);
    ch.bit = uint8_t(ch.poly4 & 1);
}

void Tia::clock_audio() {
    clock_channel(ch_[0]);
    clock_channel(ch_[1]);
    const int mix = int(ch_[0].bit) * ch_[0].audv + int(ch_[1].bit) * ch_[1].audv;
    sample_ = int16_t(mix * 800);
}

void Tia::emit_audio(int cpu_cycles, uint32_t cpu_clock, std::vector<int16_t>& dest) {
    if (cpu_clock == 0 || cpu_cycles <= 0) return;
    audio_phase_ += double(cpu_cycles) * double(kSampleRate) / double(cpu_clock);
    while (audio_phase_ >= 1.0) {
        audio_phase_ -= 1.0;
        dest.push_back(sample_);
    }
}

}  // namespace dsp
