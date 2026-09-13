#include "video/antic.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {

// Per-ANTIC-mode geometry. `ppb` is pixels per fetched byte, `hcpp` how
// many half-colour-clocks each of those pixels occupies, `lines` the
// height of a mode block, and `bpp` whether pixels are 1 or 2 bits.
// Modes 0 and 1 are blank/jump and never reach here.
struct ModeInfo { int ppb, hcpp, lines, bpp; bool text; };
constexpr ModeInfo kModes[16] = {
    {0, 0, 1, 0, false},   // 0 blank
    {0, 0, 1, 0, false},   // 1 jump
    {8, 1, 8, 1, true},    // 2 hi-res text
    {8, 1, 10, 1, true},   // 3 hi-res text, 10 lines
    {4, 2, 8, 2, true},    // 4 four-colour text
    {4, 2, 16, 2, true},   // 5 four-colour text, 16 lines
    {8, 2, 8, 1, true},    // 6 five-colour wide text
    {8, 2, 16, 1, true},   // 7 five-colour wide text, 16 lines
    {4, 8, 8, 2, false},   // 8 map, 4 colours
    {8, 4, 4, 1, false},   // 9 map, 2 colours
    {4, 4, 4, 2, false},   // A map, 4 colours
    {8, 2, 2, 1, false},   // B map, 2 colours
    {8, 2, 1, 1, false},   // C map, 2 colours
    {4, 2, 2, 2, false},   // D map, 4 colours
    {4, 2, 1, 2, false},   // E map, 4 colours
    {8, 1, 1, 1, false},   // F hi-res map
};

// Playfield width in half-colour-clocks and its offset into the visible
// window, indexed by DMACTL bits 0-1 (none / narrow / normal / wide).
constexpr int kPfWidthHc[4] = {0, 256, 320, 384};
constexpr int kPfStartHc[4] = {0, 32, 16, 0};

// The 256-entry Atari colour table (16 hues x 16 luminances), generated
// from an NTSC YIQ approximation. Hue 0 is greyscale; the others are
// spaced 25.7 degrees apart around the colour burst.
struct Palette {
    uint32_t argb[256];
    Palette() {
        for (int hue = 0; hue < 16; ++hue) {
            for (int lum = 0; lum < 16; ++lum) {
                const double y = 0.05 + 0.95 * (lum / 15.0);
                double r = y, g = y, b = y;
                if (hue != 0) {
                    const double phase = (hue * 25.7 - 58.0) * M_PI / 180.0;
                    const double sat = 0.30;
                    const double i = sat * std::cos(phase);
                    const double q = sat * std::sin(phase);
                    r = y + 0.956 * i + 0.621 * q;
                    g = y - 0.272 * i - 0.647 * q;
                    b = y - 1.106 * i + 1.703 * q;
                }
                auto clamp8 = [](double v) {
                    return uint32_t(std::clamp(int(std::lround(v * 255.0)), 0, 255));
                };
                argb[hue * 16 + lum] =
                    0xff000000u | (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(b);
            }
        }
    }
};
const Palette& palette() {
    static const Palette p;
    return p;
}

}  // namespace

Antic::Antic(Gtia& gtia) : gtia_(gtia) { reset(); }

void Antic::reset() {
    dmactl_ = chactl_ = hscrol_ = vscrol_ = 0;
    pmbase_ = chbase_ = nmien_ = nmist_ = 0;
    dlist_ = dl_ptr_ = scan_addr_ = 0;
    mode_ = 0;
    mode_line_ = mode_height_ = 0;
    dli_pending_ = hscroll_ = vscroll_ = false;
    list_done_ = false;
    vcount_ = 0;
    wsync_ = false;
}

uint8_t Antic::read(uint16_t offset) {
    switch (offset & 0x0f) {
        case 0x0b: return uint8_t(vcount_ >> 1);
        case 0x0f: return nmist_;
        default: return 0xff;
    }
}

void Antic::write(uint16_t offset, uint8_t value) {
    switch (offset & 0x0f) {
        case 0x00: dmactl_ = value; break;
        case 0x01: chactl_ = value; break;
        case 0x02: dlist_ = uint16_t((dlist_ & 0xff00) | value); break;
        case 0x03: dlist_ = uint16_t((dlist_ & 0x00ff) | (uint16_t(value) << 8)); break;
        case 0x04: hscrol_ = value & 0x0f; break;
        case 0x05: vscrol_ = value & 0x0f; break;
        case 0x07: pmbase_ = value; break;
        case 0x09: chbase_ = value; break;
        case 0x0a: wsync_ = true; break;
        case 0x0e: nmien_ = value; break;
        case 0x0f: nmist_ = 0; break;  // NMIRES clears the status latches
        default: break;
    }
}

void Antic::begin_frame() {
    dl_ptr_ = dlist_;
    mode_ = 0;
    mode_line_ = 0;
    mode_height_ = 0;
    list_done_ = false;
}

void Antic::fetch_display_list(int line) {
    // Only advance the list while display-list DMA is enabled; with it off
    // ANTIC just shows background, which is what the OS does during its
    // own screen setup.
    if (!(dmactl_ & 0x20) || list_done_) {
        mode_ = 0;
        mode_height_ = 1;
        mode_line_ = 0;
        return;
    }
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t insn = mem(dl_ptr_++);
        const uint8_t m = uint8_t(insn & 0x0f);
        dli_pending_ = (insn & 0x80) != 0;
        if (m == 0) {
            mode_ = 0;
            mode_height_ = ((insn >> 4) & 7) + 1;
            mode_line_ = 0;
            return;
        }
        if (m == 1) {
            const uint8_t lo = mem(dl_ptr_++);
            const uint8_t hi = mem(dl_ptr_++);
            const uint16_t target = uint16_t(lo | (uint16_t(hi) << 8));
            if (insn & 0x40) {
                // JVB: jump and wait for the next vertical blank.
                dlist_ = target;
                dl_ptr_ = target;
                list_done_ = true;
                mode_ = 0;
                mode_height_ = 1;
                mode_line_ = 0;
                return;
            }
            dl_ptr_ = target;
            continue;
        }
        if (insn & 0x40) {  // LMS: a new screen-data address follows
            const uint8_t lo = mem(dl_ptr_++);
            const uint8_t hi = mem(dl_ptr_++);
            scan_addr_ = uint16_t(lo | (uint16_t(hi) << 8));
        }
        hscroll_ = (insn & 0x10) != 0;
        vscroll_ = (insn & 0x20) != 0;
        mode_ = m;
        mode_height_ = kModes[m].lines;
        mode_line_ = 0;
        (void)line;
        return;
    }
    // Runaway list (no valid instruction in 64 tries): stop for this frame.
    list_done_ = true;
    mode_ = 0;
    mode_height_ = 1;
    mode_line_ = 0;
}

void Antic::render_line(uint32_t* dst) {
    const auto& pal = palette();
    const uint8_t bak = gtia_.colbk();
    for (int i = 0; i < kScreenWidth; ++i) dst[i] = pal.argb[bak];

    const int width_sel = dmactl_ & 3;
    if (mode_ < 2 || width_sel == 0) {
        // Blank line: still let GTIA overlay players on the background.
        for (int hc = 0; hc < kScreenWidth; ++hc)
            dst[hc] = pal.argb[gtia_.pixel(Gtia::kPfBak, (hc >> 1) + kFirstVisibleClock)];
        return;
    }

    const ModeInfo& mi = kModes[mode_];
    const int pf_hc = kPfWidthHc[width_sel];
    const int pf_start = kPfStartHc[width_sel];
    const int hc_per_byte = mi.ppb * mi.hcpp;
    const int bytes = hc_per_byte ? (pf_hc / hc_per_byte) : 0;

    // Fine horizontal scroll shifts the playfield left by up to 15 colour
    // clocks; ANTIC compensates by fetching from a wider window, which we
    // approximate by simply offsetting the output position.
    const int hshift = hscroll_ ? -(hscrol_ * 2) : 0;

    // Row within the character glyph / bitmap line, honouring vertical
    // scroll (which starts a block partway down).
    int row = mode_line_;
    if (vscroll_) row = (row + vscrol_) % std::max(1, mi.lines);

    const bool gtia_mode = (mode_ == 0x0f) && (gtia_.prior_mode() != 0);

    uint16_t addr = scan_addr_;
    int out = pf_start + hshift;
    for (int bx = 0; bx < bytes; ++bx) {
        uint8_t data = mem(uint16_t(addr + bx));
        uint8_t glyph = data;
        int colour_hint = -1;  // for modes 4/5/6/7 the character supplies colour

        if (mi.text) {
            uint16_t cbase;
            uint8_t code;
            if (mode_ == 6 || mode_ == 7) {
                cbase = uint16_t((chbase_ & 0xfe) << 8);
                code = uint8_t(data & 0x3f);
                colour_hint = (data >> 6) & 3;
            } else {
                cbase = uint16_t((chbase_ & 0xfc) << 8);
                code = uint8_t(data & 0x7f);
                if (mode_ == 4 || mode_ == 5) colour_hint = (data & 0x80) ? 1 : 0;
            }
            // CHACTL bit 0 mirrors the glyph vertically, so the row index
            // within the character counts backwards.
            const int grow = (chactl_ & 0x01) ? (7 - std::min(row, 7)) : std::min(row, 7);
            glyph = mem(uint16_t(cbase + code * 8 + grow));
            // In modes 2/3 the character's own bit 7 requests inverse video
            // or blanking, and CHACTL bits 1/2 say which of the two ANTIC
            // actually does. Note bit 1 is "invert these characters", NOT
            // "blank everything": the OS leaves CHACTL at $02 for normal
            // text, so treating that bit as a global blank wipes the whole
            // display (every glyph fetched, then forced to zero).
            if ((mode_ == 2 || mode_ == 3) && (data & 0x80)) {
                if (chactl_ & 0x04) glyph = 0;
                else if (chactl_ & 0x02) glyph = uint8_t(~glyph);
            }
        }

        if (gtia_mode) {
            // GTIA's own 9/10/11 modes reinterpret mode F's bytes as two
            // 4-bit pixels, each two colour clocks wide. Priority and
            // players are not merged here, which is a simplification.
            for (int half = 0; half < 2; ++half) {
                const uint8_t nib = uint8_t(half ? (data & 0x0f) : (data >> 4));
                uint8_t colour;
                switch (gtia_.prior_mode()) {
                    case 1: colour = uint8_t((gtia_.colbk() & 0xf0) | nib); break;
                    case 3: colour = uint8_t((nib << 4) | (gtia_.colbk() & 0x0f)); break;
                    default: colour = uint8_t((gtia_.colbk() & 0xf0) | (nib << 0)); break;
                }
                for (int k = 0; k < 4; ++k) {
                    const int px = out + half * 4 + k;
                    if (px >= 0 && px < kScreenWidth) dst[px] = pal.argb[colour];
                }
            }
            out += 8;
            continue;
        }

        for (int p = 0; p < mi.ppb; ++p) {
            uint8_t pf;
            if (mi.bpp == 1) {
                const bool on = (glyph & (0x80u >> p)) != 0;
                if (mode_ == 2 || mode_ == 3 || mode_ == 0x0f) {
                    pf = on ? uint8_t(Gtia::kPfHi2) : uint8_t(Gtia::kPf2);
                } else if (mode_ == 6 || mode_ == 7) {
                    pf = on ? uint8_t(Gtia::kPf0 + colour_hint) : uint8_t(Gtia::kPfBak);
                } else {
                    pf = on ? uint8_t(Gtia::kPf0) : uint8_t(Gtia::kPfBak);
                }
            } else {
                const int shift = 6 - p * 2;
                const int v = (glyph >> shift) & 3;
                if (v == 0) pf = uint8_t(Gtia::kPfBak);
                else if (v == 3 && colour_hint == 1) pf = uint8_t(Gtia::kPf3);
                else pf = uint8_t(Gtia::kPf0 + (v - 1));
            }
            for (int k = 0; k < mi.hcpp; ++k) {
                const int px = out + k;
                if (px >= 0 && px < kScreenWidth)
                    dst[px] = pal.argb[gtia_.pixel(pf, (px >> 1) + kFirstVisibleClock)];
            }
            out += mi.hcpp;
        }
    }
}

void Antic::do_pm_dma(int line) {
    if (!(dmactl_ & 0x0c)) return;
    const bool one_line = (dmactl_ & 0x10) != 0;
    const uint16_t base = uint16_t(pmbase_ << 8);
    // Two-line resolution halves the table and indexes by line/2.
    const int idx = one_line ? line : (line >> 1);
    const uint16_t span = one_line ? 0x100 : 0x80;
    if (dmactl_ & 0x04)  // missile DMA
        gtia_.set_missile(mem(uint16_t(base + span * 3 + idx)));
    if (dmactl_ & 0x08) {  // player DMA
        for (int n = 0; n < 4; ++n)
            gtia_.set_player(n, mem(uint16_t(base + span * (4 + n) + idx)));
    }
}

int Antic::scanline(int line, uint32_t* dst) {
    vcount_ = line;
    int stolen = 0;

    if (line == 248) {
        // Vertical blank starts here on NTSC.
        nmist_ = uint8_t((nmist_ & 0x3f) | 0x40);
        if (nmien_ & 0x40 && nmi_) nmi_();
    }

    const bool visible = (line >= kFirstVisibleLine) &&
                         (line < kFirstVisibleLine + kScreenHeight);

    if (line < 248) {
        if (mode_line_ >= mode_height_) {
            fetch_display_list(line);
            stolen += 1;
        }
        if (dli_pending_ && mode_height_ > 0 && mode_line_ == mode_height_ - 1) {
            nmist_ = uint8_t((nmist_ & 0x3f) | 0x80);
            if (nmien_ & 0x80 && nmi_) nmi_();
        }
    }

    if (visible) {
        do_pm_dma(line);
        gtia_.begin_line(line);
        render_line(dst);
        if (mode_ >= 2) {
            // Rough DMA cost: one cycle per fetched byte, plus the glyph
            // fetch in text modes.
            const ModeInfo& mi = kModes[mode_];
            const int hcb = mi.ppb * mi.hcpp;
            const int bytes = hcb ? (kPfWidthHc[dmactl_ & 3] / hcb) : 0;
            stolen += mi.text ? bytes * 2 : bytes;
        }
    }

    if (line < 248 && mode_height_ > 0) {
        ++mode_line_;
        if (mode_line_ >= mode_height_ && mode_ >= 2) {
            // Advance the screen pointer past the block we just finished.
            const ModeInfo& mi = kModes[mode_];
            const int hcb = mi.ppb * mi.hcpp;
            const int bytes = hcb ? (kPfWidthHc[dmactl_ & 3] / hcb) : 0;
            scan_addr_ = uint16_t(scan_addr_ + bytes);
        }
    }
    return std::min(stolen, kCyclesPerLine - 8);
}

}  // namespace dsp
