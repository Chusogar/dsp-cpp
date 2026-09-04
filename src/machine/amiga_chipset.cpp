#include "machine/amiga_chipset.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint16_t kDmaen = 0x0200;
constexpr uint16_t kBplen = 0x0100;
constexpr uint16_t kCopen = 0x0080;
constexpr uint16_t kBlten = 0x0040;
constexpr uint16_t kDsken = 0x0010;

int paula_ipl(uint16_t intena, uint16_t intreq) {
    if (!(intena & 0x4000)) return 0;
    const uint16_t bits = uint16_t(intena & intreq & 0x3FFF);
    if (!bits) return 0;
    if (bits & 0x2000) return 6;
    if (bits & 0x1800) return 5;
    if (bits & 0x0780) return 4;
    if (bits & 0x0070) return 3;
    if (bits & 0x0008) return 2;
    if (bits & 0x0007) return 1;
    return 0;
}

uint16_t minterm(uint16_t a, uint16_t b, uint16_t c, uint8_t mt) {
    uint16_t d = 0;
    for (int i = 0; i < 16; i++) {
        const int idx = int(((a >> i) & 1) << 2) | int(((b >> i) & 1) << 1) | int((c >> i) & 1);
        if (mt & (1 << idx)) d = uint16_t(d | (1u << i));
    }
    return d;
}

}  // namespace

void AmigaChipset::set_chip_handlers(ChipRead16 read, ChipWrite16 write) {
    read16_ = std::move(read);
    write16_ = std::move(write);
}

void AmigaChipset::reset() {
    dmacon_ = intena_ = intreq_ = adkcon_ = 0;
    dsklen_ = 0;
    dsksync_ = 0x4489;
    dskpt_ = cop1lc_ = cop2lc_ = coppc_ = 0;
    diwstrt_ = 0x2C81;
    diwstop_ = 0xF4C1;
    ddfstrt_ = 0x0038;
    ddfstop_ = 0x00D0;
    bplcon0_ = bplcon1_ = bplcon2_ = 0;
    bpl1mod_ = bpl2mod_ = 0;
    bplpt_.fill(0);
    color_.fill(0);
    bltcon0_ = bltcon1_ = 0;
    bltafwm_ = bltalwm_ = 0xFFFF;
    bltapt_ = bltbpt_ = bltcpt_ = bltdpt_ = 0;
    bltamod_ = bltbmod_ = bltcmod_ = bltdmod_ = 0;
    bltadat_ = bltbdat_ = bltcdat_ = 0;
    bzero_ = true;
    vpos_ = 0;
    cop_stopped_ = true;
}

uint16_t AmigaChipset::chip_read(uint32_t addr) const {
    if (read16_) return read16_(addr);
    return 0;
}

void AmigaChipset::chip_write(uint32_t addr, uint16_t value) {
    if (write16_) write16_(addr, value);
}

void AmigaChipset::poke_ptr(uint32_t& p, bool high, uint16_t value) {
    if (high)
        p = (uint32_t(value) << 16) | (p & 0xFFFF);
    else
        p = (p & 0xFFFF0000u) | value;
}

void AmigaChipset::setclr(uint16_t& reg, uint16_t value, uint16_t mask) {
    const uint16_t bits = uint16_t(value & mask);
    if (value & 0x8000)
        reg = uint16_t(reg | bits);
    else
        reg = uint16_t(reg & ~bits);
}

int AmigaChipset::ipl() const {
    uint16_t req = intreq_;
    if (ciaa_irq_) req = uint16_t(req | 0x0008);
    if (ciab_irq_) req = uint16_t(req | 0x2000);
    return paula_ipl(intena_, req);
}

uint16_t AmigaChipset::read(uint16_t reg) {
    switch (reg & 0x1FE) {
        case 0x002: {
            uint16_t v = uint16_t(dmacon_ & 0x07FF);
            if (!bzero_) v = uint16_t(v | 0x2000);
            return v;
        }
        case 0x004:
            return uint16_t(((vpos_ >> 8) & 1) | 0x0000);
        case 0x006:
            return uint16_t(uint16_t(vpos_ & 0xFF) << 8);
        case 0x00A:
        case 0x00C:
            return 0;
        case 0x010:
            return adkcon_;
        case 0x016:
            return 0xFF00;
        case 0x018:
            return 0x3000;
        case 0x01A: {
            uint16_t v = 0;
            if (dsklen_ & 0x8000) v = uint16_t(v | 0x8000);
            return v;
        }
        case 0x01C:
            return intena_;
        case 0x01E: {
            uint16_t v = intreq_;
            if (ciaa_irq_) v = uint16_t(v | 0x0008);
            if (ciab_irq_) v = uint16_t(v | 0x2000);
            return v;
        }
        default:
            if ((reg & 0x1FE) >= 0x180 && (reg & 0x1FE) <= 0x1BE) {
                return color_[((reg & 0x1FE) - 0x180) >> 1];
            }
            return 0;
    }
}

void AmigaChipset::write(uint16_t reg, uint16_t value) {
    const uint16_t r = uint16_t(reg & 0x1FE);
    switch (r) {
        case 0x020:
            poke_ptr(dskpt_, true, value);
            break;
        case 0x022:
            poke_ptr(dskpt_, false, value);
            break;
        case 0x024: {
            const uint16_t prev = dsklen_;
            dsklen_ = value;
            if ((value & 0x8000) && (prev & 0x8000) && dma(kDsken)) disk_dma();
            break;
        }
        case 0x07E:
            dsksync_ = value;
            break;
        case 0x080:
            poke_ptr(cop1lc_, true, value);
            break;
        case 0x082:
            poke_ptr(cop1lc_, false, value);
            break;
        case 0x084:
            poke_ptr(cop2lc_, true, value);
            break;
        case 0x086:
            poke_ptr(cop2lc_, false, value);
            break;
        case 0x088:
            coppc_ = cop1lc_;
            cop_stopped_ = false;
            copper_step_until_wait(vpos_);
            break;
        case 0x08A:
            coppc_ = cop2lc_;
            cop_stopped_ = false;
            copper_step_until_wait(vpos_);
            break;
        case 0x08E:
            diwstrt_ = value;
            break;
        case 0x090:
            diwstop_ = value;
            break;
        case 0x092:
            ddfstrt_ = value;
            break;
        case 0x094:
            ddfstop_ = value;
            break;
        case 0x096:
            setclr(dmacon_, value, 0x07FF);
            break;
        case 0x09A:
            setclr(intena_, value, 0x7FFF);
            break;
        case 0x09C:
            setclr(intreq_, value, 0x7FFF);
            break;
        case 0x09E:
            setclr(adkcon_, value, 0x7FFF);
            break;
        case 0x040:
            bltcon0_ = value;
            break;
        case 0x042:
            bltcon1_ = value;
            break;
        case 0x044:
            bltafwm_ = value;
            break;
        case 0x046:
            bltalwm_ = value;
            break;
        case 0x048:
            poke_ptr(bltcpt_, true, value);
            break;
        case 0x04A:
            poke_ptr(bltcpt_, false, value);
            break;
        case 0x04C:
            poke_ptr(bltbpt_, true, value);
            break;
        case 0x04E:
            poke_ptr(bltbpt_, false, value);
            break;
        case 0x050:
            poke_ptr(bltapt_, true, value);
            break;
        case 0x052:
            poke_ptr(bltapt_, false, value);
            break;
        case 0x054:
            poke_ptr(bltdpt_, true, value);
            break;
        case 0x056:
            poke_ptr(bltdpt_, false, value);
            break;
        case 0x058:
            bltsize_ = value;
            blit();
            break;
        case 0x060:
            bltcmod_ = int16_t(value);
            break;
        case 0x062:
            bltbmod_ = int16_t(value);
            break;
        case 0x064:
            bltamod_ = int16_t(value);
            break;
        case 0x066:
            bltdmod_ = int16_t(value);
            break;
        case 0x070:
            bltcdat_ = value;
            break;
        case 0x072:
            bltbdat_ = value;
            break;
        case 0x074:
            bltadat_ = value;
            break;
        case 0x100:
            bplcon0_ = value;
            break;
        case 0x102:
            bplcon1_ = value;
            break;
        case 0x104:
            bplcon2_ = value;
            break;
        case 0x108:
            bpl1mod_ = int16_t(value);
            break;
        case 0x10A:
            bpl2mod_ = int16_t(value);
            break;
        default:
            if (r >= 0x0E0 && r <= 0x0F6) {
                const int plane = (r - 0x0E0) >> 2;
                poke_ptr(bplpt_[size_t(plane)], (r & 2) == 0, value);
            } else if (r >= 0x180 && r <= 0x1BE) {
                color_[(r - 0x180) >> 1] = uint16_t(value & 0x0FFF);
            }
            break;
    }
}

void AmigaChipset::begin_frame() {
    vpos_ = 0;
    intreq_ = uint16_t(intreq_ | 0x0020);  // VERTB
    if (ciaa_irq_) intreq_ = uint16_t(intreq_ | 0x0008);
    if (ciab_irq_) intreq_ = uint16_t(intreq_ | 0x2000);
    copper_restart();
}

void AmigaChipset::copper_restart() {
    if (dma(kCopen)) {
        coppc_ = cop1lc_;
        cop_stopped_ = false;
    }
}

void AmigaChipset::copper_line(int vpos) {
    vpos_ = vpos;
    if (!dma(kCopen) || cop_stopped_) return;
    copper_step_until_wait(vpos);
}

void AmigaChipset::copper_step_until_wait(int vpos) {
    for (int n = 0; n < 16384; n++) {
        const uint16_t w1 = chip_read(coppc_);
        const uint16_t w2 = chip_read(coppc_ + 2);
        coppc_ += 4;
        if (w1 == 0xFFFF && (w2 & 0xFFFE) == 0xFFFE) {
            cop_stopped_ = true;
            return;
        }
        if ((w1 & 1) == 0) {
            const uint16_t dest = uint16_t(w1 & 0x1FE);
            if (dest >= 0x040) write(dest, w2);
            continue;
        }
        // WAIT / SKIP. IR2 bit 0 = 1 is SKIP.
        if (w2 & 1) continue;
        const int wait_v = (w1 >> 8) & 0xFF;
        const int ve = ((w2 >> 8) & 0x7F) | 0x80;
        const int masked_v = vpos & ve;
        const int masked_wait = wait_v & ve;
        if (masked_v < masked_wait && wait_v != 0xFF) {
            coppc_ -= 4;
            return;
        }
    }
    cop_stopped_ = true;
}

void AmigaChipset::blit() {
    int height = bltsize_ >> 6;
    int width = bltsize_ & 0x3F;
    if (height == 0) height = 1024;
    if (width == 0) width = 64;

    const bool desc = (bltcon1_ & 2) != 0;
    const int delta = desc ? -2 : 2;
    const int ashift = (bltcon0_ >> 12) & 0xF;
    const int bshift = (bltcon1_ >> 12) & 0xF;
    const bool usea = (bltcon0_ & 0x0800) != 0;
    const bool useb = (bltcon0_ & 0x0400) != 0;
    const bool usec = (bltcon0_ & 0x0200) != 0;
    const bool used = (bltcon0_ & 0x0100) != 0;
    const uint8_t mt = uint8_t(bltcon0_);
    const bool line = (bltcon1_ & 1) != 0;

    uint32_t apt = bltapt_, bpt = bltbpt_, cpt = bltcpt_, dpt = bltdpt_;
    bzero_ = true;

    if (line) {
        // Line mode: plot `height` pixels along BLTAPT error-term DDA into D/C.
        int xsign = (bltcon1_ & 0x10) ? -1 : 1;
        int ysign = (bltcon1_ & 0x08) ? -1 : 1;
        const bool sud = (bltcon1_ & 0x04) != 0;  // sometimes up/down vs left/right
        (void)xsign;
        (void)ysign;
        (void)sud;
        uint16_t adat = bltadat_;
        if (ashift) {
            if (desc)
                adat = uint16_t(adat << ashift);
            else
                adat = uint16_t(adat >> ashift);
        }
        for (int i = 0; i < height; i++) {
            uint16_t c = usec ? chip_read(cpt) : bltcdat_;
            uint16_t d = minterm(adat, bltbdat_, c, mt);
            if (used) chip_write(dpt, d);
            if (d) bzero_ = false;
            // Single-pixel line: step D/C by one word in the major direction.
            if (bltcon1_ & 0x04) {
                cpt = uint32_t(int32_t(cpt) + bltcmod_);
                dpt = uint32_t(int32_t(dpt) + bltdmod_);
            } else {
                cpt = uint32_t(int32_t(cpt) + delta);
                dpt = uint32_t(int32_t(dpt) + delta);
            }
        }
        bltapt_ = apt;
        bltbpt_ = bpt;
        bltcpt_ = cpt;
        bltdpt_ = dpt;
        intreq_ = uint16_t(intreq_ | 0x0040);
        return;
    }

    for (int y = 0; y < height; y++) {
        uint32_t a_hold = 0, b_hold = 0;
        for (int x = 0; x < width; x++) {
            uint16_t a_in = bltadat_;
            if (usea) {
                a_in = chip_read(apt);
                apt = uint32_t(int32_t(apt) + delta);
            }
            uint16_t mask = 0xFFFF;
            if (x == 0) mask &= bltafwm_;
            if (x == width - 1) mask &= bltalwm_;
            a_in = uint16_t(a_in & mask);

            uint16_t a_shifted;
            if (desc) {
                const uint32_t comb = (uint32_t(a_in) << 16) | a_hold;
                a_shifted = uint16_t(comb >> (16 - ashift));
                a_hold = a_in;
            } else {
                const uint32_t comb = (a_hold << 16) | a_in;
                a_shifted = uint16_t(comb >> ashift);
                a_hold = a_in;
            }

            uint16_t b_in = bltbdat_;
            if (useb) {
                b_in = chip_read(bpt);
                bpt = uint32_t(int32_t(bpt) + delta);
            }
            uint16_t b_shifted;
            if (desc) {
                const uint32_t comb = (uint32_t(b_in) << 16) | b_hold;
                b_shifted = uint16_t(comb >> (16 - bshift));
                b_hold = b_in;
            } else {
                const uint32_t comb = (b_hold << 16) | b_in;
                b_shifted = uint16_t(comb >> bshift);
                b_hold = b_in;
            }
            if (!useb) b_shifted = bltbdat_;

            uint16_t c = bltcdat_;
            if (usec) {
                c = chip_read(cpt);
                cpt = uint32_t(int32_t(cpt) + delta);
            }

            const uint16_t d = minterm(a_shifted, b_shifted, c, mt);
            if (d) bzero_ = false;
            if (used) {
                chip_write(dpt, d);
                dpt = uint32_t(int32_t(dpt) + delta);
            }
        }
        apt = uint32_t(int32_t(apt) + bltamod_);
        bpt = uint32_t(int32_t(bpt) + bltbmod_);
        cpt = uint32_t(int32_t(cpt) + bltcmod_);
        dpt = uint32_t(int32_t(dpt) + bltdmod_);
    }
    bltapt_ = apt;
    bltbpt_ = bpt;
    bltcpt_ = cpt;
    bltdpt_ = dpt;
    intreq_ = uint16_t(intreq_ | 0x0040);  // BLIT
}

void AmigaChipset::disk_dma() {
    if (!track_mfm_) {
        intreq_ = uint16_t(intreq_ | 0x0002);
        dsklen_ = uint16_t(dsklen_ & 0x7FFF);
        return;
    }
    if (dsklen_ & 0x4000) {
        intreq_ = uint16_t(intreq_ | 0x0002);
        dsklen_ = uint16_t(dsklen_ & 0x7FFF);
        return;
    }
    std::vector<uint16_t> mfm = track_mfm_();
    if (mfm.empty()) {
        intreq_ = uint16_t(intreq_ | 0x0002);
        dsklen_ = uint16_t(dsklen_ & 0x7FFF);
        return;
    }
    int start = 0;
    if (adkcon_ & 0x0400) {
        start = -1;
        for (int i = 0; i < int(mfm.size()); i++) {
            if (mfm[size_t(i)] == dsksync_) {
                start = i;
                intreq_ = uint16_t(intreq_ | 0x1000);
                break;
            }
        }
        if (start < 0) start = 0;
    }
    int words = dsklen_ & 0x3FFF;
    if (words == 0) words = 0x4000;
    uint32_t pt = dskpt_;
    for (int i = 0; i < words; i++) {
        const uint16_t w = mfm[size_t((start + i) % int(mfm.size()))];
        chip_write(pt, w);
        pt += 2;
    }
    dskpt_ = pt;
    intreq_ = uint16_t(intreq_ | 0x0002);  // DSKBLK
    dsklen_ = uint16_t(dsklen_ & 0x7FFF);
}

uint32_t AmigaChipset::rgb(uint16_t c) const {
    const int r = (c >> 8) & 0xF;
    const int g = (c >> 4) & 0xF;
    const int b = c & 0xF;
    return 0xFF000000u | uint32_t(r * 17) << 16 | uint32_t(g * 17) << 8 | uint32_t(b * 17);
}

void AmigaChipset::render(uint32_t* framebuffer) {
    // Finish any remaining copper MOVEs so a static insert-disk list is applied.
    if (dma(kCopen) && !cop_stopped_) {
        for (int n = 0; n < 8192; n++) {
            const uint16_t w1 = chip_read(coppc_);
            const uint16_t w2 = chip_read(coppc_ + 2);
            coppc_ += 4;
            if (w1 == 0xFFFF && (w2 & 0xFFFE) == 0xFFFE) break;
            if ((w1 & 1) == 0) {
                const uint16_t dest = uint16_t(w1 & 0x1FE);
                if (dest >= 0x040) write(dest, w2);
            }
        }
    }

    const int bpu = std::min(6, (bplcon0_ >> 12) & 7);
    std::array<uint32_t, 6> pt = bplpt_;
    const uint32_t bg = rgb(color_[0]);
    const bool planes = bpu > 0 && dma(kBplen);

    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x++) {
            if (!planes) {
                framebuffer[y * kWidth + x] = bg;
                continue;
            }
            const int bit = 15 - (x & 15);
            const int word = x >> 4;
            int idx = 0;
            for (int p = 0; p < bpu; p++) {
                const uint16_t w = chip_read(pt[size_t(p)] + uint32_t(word * 2));
                if (w & (1u << bit)) idx |= 1 << p;
            }
            framebuffer[y * kWidth + x] = rgb(color_[size_t(idx & 31)]);
        }
        if (planes) {
            for (int p = 0; p < bpu; p++) {
                const int16_t mod = (p & 1) ? bpl2mod_ : bpl1mod_;
                pt[size_t(p)] += uint32_t(40 + mod);
            }
        }
    }
}

}  // namespace dsp
