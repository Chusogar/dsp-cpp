#include "video/gtia.h"

#include <algorithm>

namespace dsp {

namespace {
// Player/missile width in colour clocks for the two-bit SIZEP/SIZEM codes:
// 0 and 2 are normal, 1 is double, 3 is quadruple.
constexpr int kPmWidth[4] = {1, 2, 1, 4};
}  // namespace

Gtia::Gtia() { reset(); }

void Gtia::reset() {
    for (int i = 0; i < 4; ++i) {
        colpm_[i] = colpf_[i] = 0;
        grafp_[i] = hposp_[i] = hposm_[i] = sizep_[i] = 0;
        m2pf_[i] = p2pf_[i] = m2pl_[i] = p2pl_[i] = 0;
    }
    colbk_ = 0;
    grafm_ = 0;
    sizem_ = 0;
    prior_ = vdelay_ = gractl_ = 0;
    player_mask_.fill(0);
    missile_mask_.fill(0);
}

void Gtia::update_pm_spans() {
    player_mask_.fill(0);
    missile_mask_.fill(0);
    for (int n = 0; n < 4; ++n) {
        // Each player is 8 bits wide, each bit `scale` colour clocks.
        const int scale = kPmWidth[sizep_[n] & 3];
        const uint8_t bits = grafp_[n];
        if (bits) {
            int x = hposp_[n];
            for (int b = 7; b >= 0; --b) {
                if (bits & (1u << b)) {
                    for (int k = 0; k < scale; ++k) {
                        const int px = x + k;
                        if (px >= 0 && px < int(player_mask_.size()))
                            player_mask_[size_t(px)] |= uint8_t(1u << n);
                    }
                }
                x += scale;
            }
        }
        // Missiles are 2 bits each, packed into GRAFM (missile n = bits 2n..2n+1).
        const int mscale = kPmWidth[(sizem_ >> (n * 2)) & 3];
        const uint8_t mbits = uint8_t((grafm_ >> (n * 2)) & 3);
        if (mbits) {
            int x = hposm_[n];
            for (int b = 1; b >= 0; --b) {
                if (mbits & (1u << b)) {
                    for (int k = 0; k < mscale; ++k) {
                        const int px = x + k;
                        if (px >= 0 && px < int(missile_mask_.size()))
                            missile_mask_[size_t(px)] |= uint8_t(1u << n);
                    }
                }
                x += mscale;
            }
        }
    }
}

void Gtia::begin_line(int) { update_pm_spans(); }

uint8_t Gtia::pixel(uint8_t pf, int hx) {
    const size_t idx = size_t(std::clamp(hx, 0, 255));
    const uint8_t pm = player_mask_[idx];
    const uint8_t mm = missile_mask_[idx];

    // Collision latches: record what overlaps what before priority hides it.
    if (pf >= kPf0 && pf <= kPf3) {
        const int pfn = pf - kPf0;
        for (int n = 0; n < 4; ++n) {
            if (pm & (1u << n)) p2pf_[n] |= uint8_t(1u << pfn);
            if (mm & (1u << n)) m2pf_[n] |= uint8_t(1u << pfn);
        }
    }
    for (int n = 0; n < 4; ++n) {
        if (pm & (1u << n)) p2pl_[n] |= uint8_t(pm & ~(1u << n));
        if (mm & (1u << n)) m2pl_[n] |= pm;
    }

    // Missiles assigned to the fifth player (PRIOR bit 4) take COLPF3.
    const bool fifth_player = (prior_ & 0x10) != 0;
    uint8_t player_bits = pm;
    uint8_t missile_colour_source = 0xff;
    if (mm) {
        if (fifth_player) {
            missile_colour_source = colpf_[3];
        } else {
            for (int n = 0; n < 4; ++n)
                if (mm & (1u << n)) { missile_colour_source = colpm_[n]; break; }
        }
    }

    // Lowest-numbered player wins among players.
    int top_player = -1;
    for (int n = 0; n < 4; ++n)
        if (player_bits & (1u << n)) { top_player = n; break; }

    const bool have_pm = (top_player >= 0) || (missile_colour_source != 0xff);
    const uint8_t pm_colour = (top_player >= 0) ? colpm_[top_player] : missile_colour_source;

    // Playfield colour before priority.
    uint8_t pf_colour = colbk_;
    bool pf_present = false;
    switch (pf) {
        case kPf0: pf_colour = colpf_[0]; pf_present = true; break;
        case kPf1: pf_colour = colpf_[1]; pf_present = true; break;
        case kPf2: pf_colour = colpf_[2]; pf_present = true; break;
        case kPf3: pf_colour = colpf_[3]; pf_present = true; break;
        case kPfHi2:
            // Mode F: the playfield supplies luminance only, hue comes from
            // COLPF2 and the pixel's luminance from COLPF1.
            pf_colour = uint8_t((colpf_[2] & 0xf0) | (colpf_[1] & 0x0f));
            pf_present = true;
            break;
        default: break;
    }

    if (!have_pm) return pf_colour;
    if (!pf_present) return pm_colour;

    // PRIOR bits 0-3 select one of four priority arrangements. Bit 0 is the
    // common "players in front of all playfield" case, bit 1 puts players
    // 0/1 in front but 2/3 behind PF0/PF1, bit 2 puts the playfield in
    // front of every player, bit 3 is the mixed arrangement.
    const uint8_t sel = uint8_t(prior_ & 0x0f);
    const bool pf01 = (pf == kPf0 || pf == kPf1);
    const int pn = (top_player >= 0) ? top_player : 0;
    bool pm_in_front;
    if (sel & 0x01) {
        pm_in_front = true;
    } else if (sel & 0x02) {
        pm_in_front = (pn < 2) || !pf01;
    } else if (sel & 0x04) {
        pm_in_front = false;
    } else if (sel & 0x08) {
        pm_in_front = pf01 ? (pn < 2) : true;
    } else {
        pm_in_front = true;
    }
    return pm_in_front ? pm_colour : pf_colour;
}

uint8_t Gtia::read(uint16_t offset) {
    switch (offset & 0x1f) {
        case 0x00: return m2pf_[0]; case 0x01: return m2pf_[1];
        case 0x02: return m2pf_[2]; case 0x03: return m2pf_[3];
        case 0x04: return p2pf_[0]; case 0x05: return p2pf_[1];
        case 0x06: return p2pf_[2]; case 0x07: return p2pf_[3];
        case 0x08: return m2pl_[0]; case 0x09: return m2pl_[1];
        case 0x0a: return m2pl_[2]; case 0x0b: return m2pl_[3];
        case 0x0c: return p2pl_[0]; case 0x0d: return p2pl_[1];
        case 0x0e: return p2pl_[2]; case 0x0f: return p2pl_[3];
        case 0x10: case 0x11: case 0x12: case 0x13:  // TRIG0-3
            return trigger_read_ ? trigger_read_(int(offset & 3)) : 1;
        case 0x14: return 0x0f;  // PAL/NTSC flag: 0x0f = NTSC
        case 0x1f: return console_read_ ? console_read_() : 0x07;
        default: return 0x0f;
    }
}

void Gtia::write(uint16_t offset, uint8_t value) {
    switch (offset & 0x1f) {
        case 0x00: hposp_[0] = value; break;
        case 0x01: hposp_[1] = value; break;
        case 0x02: hposp_[2] = value; break;
        case 0x03: hposp_[3] = value; break;
        case 0x04: hposm_[0] = value; break;
        case 0x05: hposm_[1] = value; break;
        case 0x06: hposm_[2] = value; break;
        case 0x07: hposm_[3] = value; break;
        case 0x08: sizep_[0] = value; break;
        case 0x09: sizep_[1] = value; break;
        case 0x0a: sizep_[2] = value; break;
        case 0x0b: sizep_[3] = value; break;
        case 0x0c: sizem_ = value; break;
        case 0x0d: grafp_[0] = value; break;
        case 0x0e: grafp_[1] = value; break;
        case 0x0f: grafp_[2] = value; break;
        case 0x10: grafp_[3] = value; break;
        case 0x11: grafm_ = value; break;
        case 0x12: colpm_[0] = value; break;
        case 0x13: colpm_[1] = value; break;
        case 0x14: colpm_[2] = value; break;
        case 0x15: colpm_[3] = value; break;
        case 0x16: colpf_[0] = value; break;
        case 0x17: colpf_[1] = value; break;
        case 0x18: colpf_[2] = value; break;
        case 0x19: colpf_[3] = value; break;
        case 0x1a: colbk_ = value; break;
        case 0x1b: prior_ = value; break;
        case 0x1c: vdelay_ = value; break;
        case 0x1d: gractl_ = value; break;
        case 0x1e:  // HITCLR
            for (int i = 0; i < 4; ++i) m2pf_[i] = p2pf_[i] = m2pl_[i] = p2pl_[i] = 0;
            break;
        default: break;
    }
}

}  // namespace dsp
