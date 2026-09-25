#include "video/tia.h"

#include <algorithm>

namespace dsp {
namespace {

// Standard NTSC TIA palette (Stella), 16 hues x 8 luminances. Bit 0 of
// COLUxx is ignored.
constexpr uint32_t kNtsc[128] = {
    0xFF000000, 0xFF4A4A4A, 0xFF6F6F6F, 0xFF8E8E8E, 0xFFAAAAAA, 0xFFC0C0C0, 0xFFD6D6D6, 0xFFECECEC,
    0xFF484800, 0xFF69690F, 0xFF86861D, 0xFFA2A22A, 0xFFBBBB35, 0xFFD2D240, 0xFFE8E84A, 0xFFFCFC54,
    0xFF7C2C00, 0xFF904811, 0xFFA26221, 0xFFB47A30, 0xFFC3903D, 0xFFD2A44A, 0xFFDFB755, 0xFFECC860,
    0xFF901C00, 0xFFA33915, 0xFFB55328, 0xFFC66C3A, 0xFFD5824A, 0xFFE39759, 0xFFF0AA67, 0xFFFCBC74,
    0xFF940000, 0xFFA71A1A, 0xFFB83232, 0xFFC84848, 0xFFD65C5C, 0xFFE46F6F, 0xFFF08080, 0xFFFC9090,
    0xFF840064, 0xFF97197A, 0xFFA8308F, 0xFFB846A2, 0xFFC659B3, 0xFFD46CC3, 0xFFE07CD2, 0xFFEC8CE0,
    0xFF500084, 0xFF68199A, 0xFF7D30AD, 0xFF9246C0, 0xFFA459D0, 0xFFB56CE0, 0xFFC57CEE, 0xFFD48CFC,
    0xFF140090, 0xFF331AA3, 0xFF4E32B5, 0xFF6848C6, 0xFF7F5CD5, 0xFF956FE3, 0xFFA980F0, 0xFFBC90FC,
    0xFF000094, 0xFF181AA7, 0xFF2D32B8, 0xFF4248C8, 0xFF545CD6, 0xFF656FE4, 0xFF7580F0, 0xFF8490FC,
    0xFF001C88, 0xFF183B9D, 0xFF2D57B0, 0xFF4272C2, 0xFF548AD2, 0xFF65A0E1, 0xFF75B5EF, 0xFF84C8FC,
    0xFF003064, 0xFF185080, 0xFF2D6D98, 0xFF4288B0, 0xFF54A0C5, 0xFF65B7D9, 0xFF75CCEB, 0xFF84E0FC,
    0xFF004030, 0xFF18624E, 0xFF2D8169, 0xFF429E82, 0xFF54B899, 0xFF65D1AE, 0xFF75E7C2, 0xFF84FCD4,
    0xFF004400, 0xFF1A661A, 0xFF328432, 0xFF48A048, 0xFF5CBA5C, 0xFF6FD26F, 0xFF80E880, 0xFF90FC90,
    0xFF143C00, 0xFF355F18, 0xFF527E2D, 0xFF6E9C42, 0xFF87B754, 0xFF9ED065, 0xFFB4E775, 0xFFC8FC84,
    0xFF303800, 0xFF505916, 0xFF6D762B, 0xFF88923E, 0xFFA0AB4F, 0xFFB7C25F, 0xFFCCD86E, 0xFFE0EC7C,
    0xFF482C00, 0xFF694D14, 0xFF866A26, 0xFFA28638, 0xFFBB9F47, 0xFFD2B656, 0xFFE8CC63, 0xFFFCE070,
};

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
    grp_old_[0] = grp_old_[1] = 0;
    enam_[0] = enam_[1] = 0;
    enabl_ = enabl_old_ = 0;
    hm_.fill(0);
    for (Object& o : obj_) o = Object{};
    movement_ = false;
    movement_clock_ = 0;
    extended_hblank_ = false;
    queue_.clear();
    vdelp_[0] = vdelp_[1] = false;
    vdelbl_ = false;
    resmp_[0] = resmp_[1] = false;
    hclock_ = 0;
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
    if (pressed && latch_inputs_) inpt4_latched_ = true;
}

void Tia::set_inpt5(bool pressed) {
    inpt5_ = pressed;
    if (pressed && latch_inputs_) inpt5_latched_ = true;
}

void Tia::set_hclock(int color_clocks) {
    if (color_clocks > hclock_) run(color_clocks - hclock_);
    else hclock_ = color_clocks;
}

void Tia::begin_line() {
    hclock_ = 0;
    extended_hblank_ = false;
}

int Tia::player_scale(uint8_t nusiz) {
    switch (nusiz & 7) {
        case 5: return 2;
        case 7: return 4;
        default: return 1;
    }
}

// Counter values that start a copy: 156 is the main copy (seen 160 clocks
// after a reset, i.e. on the next line), 12/28/60 the close/medium/wide
// NUSIZ copies (16/32/64 pixels further right).
bool Tia::decodes(int i, int counter) const {
    if (counter == 156) return true;
    if (i == BL) return false;
    switch (nusiz_[i & 1] & 7) {
        case 1: return counter == 12;
        case 2: return counter == 28;
        case 3: return counter == 12 || counter == 28;
        case 4: return counter == 60;
        case 6: return counter == 28 || counter == 60;
        default: return false;
    }
}

void Tia::tick_object(int i) {
    Object& o = obj_[size_t(i)];
    if (i == M0 || i == M1) {
        const int p = i - M0;
        if (resmp_[p]) {
            // Locked to the centre of its player.
            const int scale = player_scale(nusiz_[p]);
            const int lag = scale == 1 ? 4 : (scale == 2 ? 8 : 12);
            o.counter = (obj_[size_t(p)].counter + 160 - lag) % 160;
            o.rendering = false;
        }
    }
    if (decodes(i, o.counter)) {
        o.rendering = true;
        // Players show their first pixel 6 clocks after the decode, missiles
        // and the ball 5.
        o.render = i <= P1 ? -6 : -5;
    } else if (o.rendering) {
        o.render++;
        int width;
        if (i <= P1) width = 8 * player_scale(nusiz_[i]) + (player_scale(nusiz_[i]) > 1 ? 1 : 0);
        else if (i == BL) width = 1 << ((ctrlpf_ >> 4) & 3);
        else width = 1 << ((nusiz_[i - M0] >> 4) & 3);
        if (o.render >= width) o.rendering = false;
    }
    o.counter = o.counter + 1 >= 160 ? 0 : o.counter + 1;
}

bool Tia::player_on(int which) const {
    const Object& o = obj_[size_t(which)];
    if (!o.rendering || o.render < 0) return false;
    const uint8_t grp = vdelp_[which] ? grp_old_[which] : grp_[which];
    const int scale = player_scale(nusiz_[which]);
    int bit;
    if (scale > 1) {
        if (o.render < 1) return false;
        bit = (o.render - 1) / scale;
    } else {
        bit = o.render;
    }
    if (bit > 7) return false;
    if (refp_[which] & 0x08) bit = 7 - bit;
    return ((grp >> (7 - bit)) & 1) != 0;
}

bool Tia::missile_on(int which) const {
    const Object& o = obj_[size_t(M0 + which)];
    if (resmp_[which] || !(enam_[which] & 0x02)) return false;
    return o.rendering && o.render >= 0;
}

bool Tia::ball_on() const {
    const Object& o = obj_[BL];
    const uint8_t en = vdelbl_ ? enabl_old_ : enabl_;
    return (en & 0x02) && o.rendering && o.render >= 0;
}

bool Tia::playfield_on(int x) const {
    int cell = x / 4;
    if (cell >= 20) cell = (ctrlpf_ & 0x01) ? (39 - cell) : (cell - 20);
    if (cell < 4) return ((pf0_ >> (4 + cell)) & 1) != 0;
    if (cell < 12) return ((pf1_ >> (11 - cell)) & 1) != 0;
    return ((pf2_ >> (cell - 12)) & 1) != 0;
}

void Tia::clock(int c) {
    // Delayed register writes (see write()).
    if (!queue_.empty()) {
        for (size_t i = 0; i < queue_.size();) {
            if (--queue_[i].clocks <= 0) {
                const Delayed d = queue_[i];
                queue_.erase(queue_.begin() + std::ptrdiff_t(i));
                apply(d.reg, d.value);
            } else {
                i++;
            }
        }
    }

    const bool hblank = c < kHblankClocks + (extended_hblank_ ? 8 : 0);

    // HMOVE ripple counter: one step every 4 clocks; an object still moving
    // takes an extra counter clock, but only while HBLANK stops its normal
    // clock.
    if (movement_ && (c & 3) == 0) {
        const int step = movement_clock_ > 15 ? 0 : movement_clock_;
        bool any = false;
        for (int i = 0; i < 5; i++) {
            Object& o = obj_[size_t(i)];
            if (step == o.hmm_clocks) o.moving = false;
            if (o.moving) {
                if (hblank) tick_object(i);
                any = true;
            }
        }
        movement_ = any;
        movement_clock_++;
    }

    if (c < kHblankClocks) return;
    const int x = c - kHblankClocks;
    if (hblank) {
        line_[size_t(x)] = 0xFF000000;  // HMOVE comb
        return;
    }
    for (int i = 0; i < 5; i++) tick_object(i);
    line_[size_t(x)] = pixel(x);
}

void Tia::run(int clocks) {
    int end = hclock_ + clocks;
    if (end > kColorClocksPerLine) end = kColorClocksPerLine;
    for (int c = hclock_; c < end; c++) {
        hclock_ = c;
        clock(c);
    }
    hclock_ = end;
}

void Tia::render_line(uint32_t* dest) {
    run(kColorClocksPerLine - hclock_);
    std::copy(line_.begin(), line_.end(), dest);
}

uint32_t Tia::pixel(int x) {
    const bool pf = playfield_on(x);
    const bool p0 = player_on(0);
    const bool p1 = player_on(1);
    const bool m0 = missile_on(0);
    const bool m1 = missile_on(1);
    const bool bl = ball_on();

    if (m0 && p1) cx_[0] |= 0x80;
    if (m0 && p0) cx_[0] |= 0x40;
    if (m1 && p0) cx_[1] |= 0x80;
    if (m1 && p1) cx_[1] |= 0x40;
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

    if (blanked()) return 0xFF000000;

    // Score mode colours the playfield with the player colours, except when
    // the playfield has priority (the real chip ignores SCORE then).
    const bool score = (ctrlpf_ & 0x06) == 0x02;
    const uint8_t pf_colu = score ? colup_[x < 80 ? 0 : 1] : colupf_;
    uint8_t colu;
    if (ctrlpf_ & 0x04) {
        if (pf || bl) colu = pf ? pf_colu : colupf_;
        else if (p0 || m0) colu = colup_[0];
        else if (p1 || m1) colu = colup_[1];
        else colu = colubk_;
    } else {
        if (p0 || m0) colu = colup_[0];
        else if (p1 || m1) colu = colup_[1];
        else if (pf) colu = pf_colu;
        else if (bl) colu = colupf_;
        else colu = colubk_;
    }
    return ntsc_color(colu);
}

void Tia::reset_object(int index) {
    // The counter restarts so that the object shows up 5 (players) or 4
    // (missiles, ball) pixels later from the next line on, or at the left
    // edge (3 / 2) when strobed during HBLANK.
    Object& o = obj_[size_t(index)];
    if (hclock_ < kHblankClocks) o.counter = 159;
    else if (hclock_ < kHblankClocks + (extended_hblank_ ? 8 : 0)) o.counter = 158;
    else o.counter = 157;
    // A reset while a player copy is still in its start-up delay restarts
    // that delay (Andrew Towers' TIA notes; Stella does the same).
    if (index <= P1 && o.rendering && o.render <= -3) o.render = -6;
}

// Stella's TIA write delays (colour clocks until the chip sees the value).
static int write_delay(uint8_t reg) {
    switch (reg) {
        case 0x01: return 1;                       // VBLANK
        case 0x0b: case 0x0c: return 1;            // REFP
        case 0x0d: case 0x0e: case 0x0f: return 2; // PF
        case 0x1b: case 0x1c: return 1;            // GRP
        case 0x1d: case 0x1e: case 0x1f: return 1; // ENAM/ENABL
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: return 2;  // HMxx
        case 0x2b: return 2;                       // HMCLR
        default: return 0;
    }
}

void Tia::write(uint8_t reg, uint8_t value) {
    reg &= 0x3f;
    const int delay = write_delay(reg);
    if (delay == 0) apply(reg, value);
    else queue_.push_back({reg, value, delay + 1});
}

void Tia::apply(uint8_t reg, uint8_t value) {
    switch (reg & 0x3f) {
        case 0x00: vsync_ = (value & 0x02) != 0; break;
        case 0x01:
            vblank_ = (value & 0x02) != 0;
            latch_inputs_ = (value & 0x40) != 0;
            dump_ports_ = (value & 0x80) != 0;
            if (!latch_inputs_) inpt4_latched_ = inpt5_latched_ = false;
            else {
                if (inpt4_) inpt4_latched_ = true;
                if (inpt5_) inpt5_latched_ = true;
            }
            break;
        case 0x02: wsync_ = true; break;
        case 0x03: break;  // RSYNC: handled by the driver
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
        case 0x10: reset_object(P0); break;
        case 0x11: reset_object(P1); break;
        case 0x12: reset_object(M0); break;
        case 0x13: reset_object(M1); break;
        case 0x14: reset_object(BL); break;
        case 0x15: ch_[0].audc = value & 0x0f; break;
        case 0x16: ch_[1].audc = value & 0x0f; break;
        case 0x17: ch_[0].audf = value & 0x1f; break;
        case 0x18: ch_[1].audf = value & 0x1f; break;
        case 0x19: ch_[0].audv = value & 0x0f; break;
        case 0x1a: ch_[1].audv = value & 0x0f; break;
        case 0x1b:
            grp_[0] = value;
            grp_old_[1] = grp_[1];
            break;
        case 0x1c:
            grp_[1] = value;
            grp_old_[0] = grp_[0];
            enabl_old_ = enabl_;
            break;
        case 0x1d: enam_[0] = value; break;
        case 0x1e: enam_[1] = value; break;
        case 0x1f: enabl_ = value; break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: {
            const int i = (reg & 0x3f) - 0x20;
            hm_[size_t(i)] = value;
            obj_[size_t(i)].hmm_clocks = ((value >> 4) ^ 0x08) & 0x0f;
            break;
        }
        case 0x25: vdelp_[0] = (value & 0x01) != 0; break;
        case 0x26: vdelp_[1] = (value & 0x01) != 0; break;
        case 0x27: vdelbl_ = (value & 0x01) != 0; break;
        case 0x28: resmp_[0] = (value & 0x02) != 0; break;
        case 0x29: resmp_[1] = (value & 0x02) != 0; break;
        case 0x2a:
            // HMOVE: start the ripple counter; strobed during HBLANK it also
            // stretches this line's HBLANK by 8 clocks (the black comb).
            movement_ = true;
            movement_clock_ = 0;
            for (Object& o : obj_) o.moving = true;
            if (hclock_ < kHblankClocks) extended_hblank_ = true;
            break;
        case 0x2b:
            hm_.fill(0);
            for (Object& o : obj_) o.hmm_clocks = 8;
            break;
        case 0x2c: cx_.fill(0); break;
        default: break;
    }
}

uint8_t Tia::read(uint8_t reg) const {
    reg &= 0x0f;
    if (reg <= 0x07) return cx_[reg];
    if (reg == 0x0c) return (inpt4_ || inpt4_latched_) ? 0x00 : 0x80;
    if (reg == 0x0d) return (inpt5_ || inpt5_latched_) ? 0x00 : 0x80;
    // INPT0-3: paddle capacitors; grounded while dumped, charged otherwise.
    if (reg >= 0x08 && reg <= 0x0b) return dump_ports_ ? 0x00 : 0x80;
    return 0x00;
}

// TIA audio (after Ron Fries' TIASound): AUDF divides the 31.4 kHz clock;
// AUDC selects the clock modifier (always / div31 / 5-bit poly) and the
// output (pure toggle / 4-bit / 5-bit / 9-bit poly), with C-F dividing by 3.
void Tia::clock_channel(Channel& ch) {
    const int max = (ch.audf + 1) * ((ch.audc & 0x0c) == 0x0c ? 3 : 1);
    if (++ch.divider < max) return;
    ch.divider = 0;

    const uint8_t fb5 = uint8_t(((ch.poly5 >> 0) ^ (ch.poly5 >> 2)) & 1);
    ch.poly5 = uint8_t((ch.poly5 >> 1) | (fb5 << 4));
    const bool bit5 = (ch.poly5 & 1) != 0;
    const bool div31 = ch.poly5 == 0x1f;

    const uint8_t c = ch.audc;
    if (c == 0x00 || c == 0x0b) {
        ch.bit = 1;
        return;
    }
    const bool tick = !(c & 0x02) || (!(c & 0x01) && div31) || ((c & 0x01) && bit5);
    if (!tick) return;
    if (c & 0x04) {
        ch.bit ^= 1;
    } else if (c & 0x08) {
        if (c == 0x08) {
            const uint16_t fb9 = uint16_t(((ch.poly9 >> 0) ^ (ch.poly9 >> 4)) & 1);
            ch.poly9 = uint16_t((ch.poly9 >> 1) | (fb9 << 8));
            ch.bit = uint8_t(ch.poly9 & 1);
        } else {
            ch.bit = bit5 ? 1 : 0;
        }
    } else {
        const uint8_t fb4 = uint8_t(((ch.poly4 >> 0) ^ (ch.poly4 >> 1)) & 1);
        ch.poly4 = uint8_t((ch.poly4 >> 1) | (fb4 << 3));
        ch.bit = uint8_t(ch.poly4 & 1);
    }
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
