#include "machine/amiga_chipset.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace dsp {
namespace {

constexpr uint16_t kDmaen = 0x0200;
constexpr uint16_t kBplen = 0x0100;
constexpr uint16_t kCopen = 0x0080;
constexpr uint16_t kBlten = 0x0040;
constexpr uint16_t kSpren = 0x0020;
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
    disk_dma_count_ = 0;
    disk_dma_empty_ = 0;
    blit_count_ = 0;
    last_bltsize_ = 0;
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
    lof_ = false;
    cop_stopped_ = true;
    copper_active_ = false;
    for (AudioChannel& a : aud_) a = AudioChannel{};
    aud_irq_ = false;
    aud_sum_ = aud_cck_ = 0;
    sprpt_.fill(0);
    sprpos_.fill(0);
    sprctl_.fill(0);
    sprdata_.fill(0);
    sprdatb_.fill(0);
    for (auto& row : spr_line_on_) row.fill(0);
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
        p = (p & 0xFFFF0000u) | (value & 0xFFFEu);  // DMA pointers address words
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
            return uint16_t((lof_ ? 0x8000 : 0) | ((vpos_ >> 8) & 1));
        case 0x006:
            // Low byte is HPOS. Return mid-line so beam-sync loops that wait
            // for a non-zero horizontal position can leave the wait.
            return uint16_t(uint16_t(vpos_ & 0xFF) << 8) | 0x80;
        case 0x00A:
            return joy0dat_;
        case 0x00C:
            return joy1dat_;
        case 0x010:
            return adkcon_;
        case 0x016:
            return rmb_ ? uint16_t(0xFF00 & ~0x0400) : uint16_t(0xFF00);
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
        case 0x036:  // JOYTEST
            if (joytest_) joytest_(value);
            break;
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
            if (!copper_active_) copper_step_until_wait(vpos_);
            break;
        case 0x08A:
            coppc_ = cop2lc_;
            cop_stopped_ = false;
            if (!copper_active_) copper_step_until_wait(vpos_);
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
        case 0x096: {
            const uint16_t old = dmacon_;
            setclr(dmacon_, value, 0x07FF);
            audio_dma_changed(old);
            break;
        }
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
            bltcmod_ = int16_t(value & 0xFFFE);  // bit 0 is not implemented
            break;
        case 0x062:
            bltbmod_ = int16_t(value & 0xFFFE);  // bit 0 is not implemented
            break;
        case 0x064:
            bltamod_ = int16_t(value & 0xFFFE);  // bit 0 is not implemented
            break;
        case 0x066:
            bltdmod_ = int16_t(value & 0xFFFE);  // bit 0 is not implemented
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
            bpl1mod_ = int16_t(value & 0xFFFE);  // bit 0 is not implemented
            break;
        case 0x10A:
            bpl2mod_ = int16_t(value & 0xFFFE);  // bit 0 is not implemented
            break;
        default:
            if (r >= 0x0E0 && r <= 0x0F6) {
                const int plane = (r - 0x0E0) >> 2;
                poke_ptr(bplpt_[size_t(plane)], (r & 2) == 0, value);
            } else if (r >= 0x120 && r <= 0x13E) {
                const int s = (r - 0x120) >> 2;
                poke_ptr(sprpt_[size_t(s)], (r & 2) == 0, value);
            } else if (r >= 0x140 && r <= 0x17E) {
                const int s = (r - 0x140) >> 3;
                switch (r & 6) {
                    case 0:
                        sprpos_[size_t(s)] = value;
                        break;
                    case 2:
                        sprctl_[size_t(s)] = value;
                        break;
                    case 4:
                        sprdata_[size_t(s)] = value;
                        break;
                    default:
                        sprdatb_[size_t(s)] = value;
                        break;
                }
            } else if (r >= 0x180 && r <= 0x1BE) {
                color_[(r - 0x180) >> 1] = uint16_t(value & 0x0FFF);
            } else if (r >= 0x0A0 && r < 0x0E0) {
                AudioChannel& a = aud_[size_t((r - 0x0A0) >> 4)];
                switch (r & 0x0E) {
                    case 0x0: a.lc = (uint32_t(value) << 16) | (a.lc & 0xFFFF); break;
                    case 0x2: a.lc = (a.lc & 0xFFFF0000u) | (value & 0xFFFEu); break;
                    case 0x4: a.len = value; break;
                    case 0x6: a.per = value; break;
                    case 0x8: a.vol = uint16_t(std::min<uint16_t>(value & 0x7F, 64)); break;
                    case 0xA:
                        // AUDxDAT with DMA off feeds the DAC directly; the
                        // channel interrupt asks for the next word.
                        if (!a.dma_on) {
                            if (a.manual) {
                                a.manual_dat = value;
                                a.manual_next = true;
                            } else {
                                a.manual = true;
                                a.dat = value;
                                a.byte = 0;
                                a.out = int8_t(value >> 8);
                                a.counter = a.per ? a.per : 65536;
                            }
                        } else {
                            a.dat = value;
                        }
                        break;
                    default: break;
                }
            }
            break;
    }
}

void AmigaChipset::begin_frame() {
    vpos_ = 0;
    lof_ = !lof_;
    intreq_ = uint16_t(intreq_ | 0x0020);  // VERTB
    if (ciaa_irq_) intreq_ = uint16_t(intreq_ | 0x0008);
    if (ciab_irq_) intreq_ = uint16_t(intreq_ | 0x2000);
    for (auto& row : spr_line_on_) row.fill(0);
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
    sprite_dma_line(vpos);
    if (!dma(kCopen) || cop_stopped_) return;
    copper_step_until_wait(vpos);
}

void AmigaChipset::sprite_dma_line(int vpos) {
    const int y = vpos - kFirstLine;
    for (int s = 0; s < 8; s++) {
        auto& pt = sprpt_[size_t(s)];
        auto& pos = sprpos_[size_t(s)];
        auto& ctl = sprctl_[size_t(s)];
        const int vstart = int((pos >> 8) & 0xFF) | ((ctl & 4) ? 0x100 : 0);
        const int vstop = int((ctl >> 8) & 0xFF) | ((ctl & 2) ? 0x100 : 0);
        if (dma(kSpren)) {
            // copinit writes SPRxPT around line 12 (still vblank). Fetch
            // POS/CTL after that so we do not walk the dummy $FE00/$FF00
            // terminator into following chip RAM.
            if (vpos == 20) {
                pos = chip_read(pt);
                ctl = chip_read(pt + 2);
                pt += 4;
            } else if (vstop != 0 && vpos == vstop && vpos != 20) {
                pos = chip_read(pt);
                ctl = chip_read(pt + 2);
                pt += 4;
                sprdata_[size_t(s)] = 0;
                sprdatb_[size_t(s)] = 0;
            } else if (vpos > 20 && vpos >= vstart && vpos < vstop) {
                sprdata_[size_t(s)] = chip_read(pt);
                sprdatb_[size_t(s)] = chip_read(pt + 2);
                pt += 4;
            }
        }
        if (y >= 0 && y < kHeight) {
            const int vs = int((pos >> 8) & 0xFF) | ((ctl & 4) ? 0x100 : 0);
            const int ve = int((ctl >> 8) & 0xFF) | ((ctl & 2) ? 0x100 : 0);
            const bool on = vpos >= vs && vpos < ve && (sprdata_[size_t(s)] || sprdatb_[size_t(s)]);
            spr_line_data_[size_t(s)][size_t(y)] = sprdata_[size_t(s)];
            spr_line_datb_[size_t(s)][size_t(y)] = sprdatb_[size_t(s)];
            spr_line_pos_[size_t(s)][size_t(y)] = pos;
            spr_line_ctl_[size_t(s)][size_t(y)] = ctl;
            spr_line_on_[size_t(s)][size_t(y)] = on ? 1 : 0;
        }
    }
}

void AmigaChipset::copper_step_until_wait(int vpos) {
    if (copper_active_) return;
    copper_active_ = true;
    int nops = 0;
    // ~113 copper slots fit in an OCS scanline. A 32-MOVE cap was cutting
    // LoadView palettes in half (32 COLOR registers plus BPLxPT).
    for (int n = 0; n < 80; n++) {
        const uint32_t pc = coppc_ & 0x000FFFFEu;
        const uint16_t w1 = chip_read(pc);
        const uint16_t w2 = chip_read(pc + 2);
        coppc_ = pc + 4;
        if (w1 == 0xFFFF && (w2 & 0xFFFE) == 0xFFFE) {
            cop_stopped_ = true;
            copper_active_ = false;
            return;
        }
        if ((w1 & 1) == 0) {
            const uint16_t dest = uint16_t(w1 & 0x1FE);
            if (dest < 0x040) {
                if (++nops >= 4) {
                    cop_stopped_ = true;
                    copper_active_ = false;
                    return;
                }
                continue;
            }
            nops = 0;
            if (dest == 0x088) {
                coppc_ = cop1lc_;
                continue;
            }
            if (dest == 0x08A) {
                coppc_ = cop2lc_;
                continue;
            }
            write(dest, w2);
            continue;
        }
        nops = 0;
        // WAIT / SKIP. IR2 bit 0 = 1 is SKIP.
        if (w2 & 1) continue;
        const int wait_v = (w1 >> 8) & 0xFF;
        const int ve = ((w2 >> 8) & 0x7F) | 0x80;
        const int masked_v = vpos & ve;
        const int masked_wait = wait_v & ve;
        if (masked_v < masked_wait) {
            coppc_ -= 4;
            copper_active_ = false;
            return;
        }
        // Horizontal WAIT: yield the rest of this line (HPOS is not modelled).
        if ((w1 & 0xFE) != 0) {
            copper_active_ = false;
            return;
        }
    }
    copper_active_ = false;
}

void AmigaChipset::blit() {
    blit_count_++;
    last_bltsize_ = bltsize_;
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

    // Agnus blit pointers are word addresses; bit 0 is ignored.
    uint32_t apt = bltapt_ & ~1u, bpt = bltbpt_ & ~1u, cpt = bltcpt_ & ~1u, dpt = bltdpt_ & ~1u;
    bzero_ = true;
    std::vector<std::pair<uint32_t, uint16_t>> deferred;

    if (line) {
        // Line mode (HRM / WinUAE blitter_line): BLTAPT low word is the
        // Bresenham accumulator (starts at 2*dy-dx, sign in BLTCON1 bit 6),
        // BLTBMOD = 4*dy is added while it is negative, BLTAMOD = 4*(dy-dx)
        // otherwise. The pixel is BLTADAT >> ASH inside the word at BLTCPT;
        // BLTBDAT is the texture (rotated by BSH). The octant bits pick the
        // major axis (SUD) and directions (SUL, AUL). SING draws one dot per
        // raster line, for area-fill outlines. BLTSIZE height = length.
        int16_t acc = int16_t(bltapt_ & 0xFFFF);
        bool sign = (bltcon1_ & 0x40) != 0;
        const bool sing = (bltcon1_ & 0x02) != 0;
        const bool sud = (bltcon1_ & 0x10) != 0;
        const bool sul = (bltcon1_ & 0x08) != 0;
        const bool aul = (bltcon1_ & 0x04) != 0;
        int ash = ashift;
        int bsh = bshift;
        uint32_t cur = cpt;
        uint32_t dout = dpt;
        bool onedot = false;
        auto incx = [&]() { if (++ash == 16) { ash = 0; cur += 2; } };
        auto decx = [&]() { if (ash-- == 0) { ash = 15; cur -= 2; } };
        auto incy = [&]() { cur = uint32_t(int32_t(cur) + bltcmod_); onedot = false; };
        auto decy = [&]() { cur = uint32_t(int32_t(cur) - bltcmod_); onedot = false; };
        for (int i = 0; i < height; i++) {
            const uint16_t a = uint16_t((bltadat_ & bltafwm_) >> ash);
            const uint16_t b = ((bltbdat_ >> bsh) & 1) ? 0xFFFF : 0x0000;
            const uint16_t c = usec ? chip_read(cur & ~1u) : bltcdat_;
            const uint16_t d = minterm(a, b, c, mt);
            if (d) bzero_ = false;
            if (used && (!sing || !onedot)) chip_write(dout & ~1u, d);
            onedot = true;
            // Step the accumulator and the position.
            if (!sign) acc = int16_t(acc + bltamod_);
            else acc = int16_t(acc + bltbmod_);
            if (!sign) {
                if (sud) { if (sul) decy(); else incy(); }
                else { if (sul) decx(); else incx(); }
            }
            if (sud) { if (aul) decx(); else incx(); }
            else { if (aul) decy(); else incy(); }
            sign = acc < 0;
            bsh = (bsh - 1) & 15;
            dout = cur;
        }
        bltapt_ = (bltapt_ & 0xFFFF0000u) | uint16_t(acc);
        bltcpt_ = cur;
        bltdpt_ = cur;
        bltcon0_ = uint16_t((bltcon0_ & 0x0FFF) | (ash << 12));
        bltcon1_ = uint16_t((bltcon1_ & 0x0FBF) | (bsh << 12) | (sign ? 0x40 : 0));
        return;
    }

    // Area fill (descending mode only): BLTCON1 IFE (bit 3) / EFE (bit 4),
    // FCI (bit 2) is the fill carry at the start of every line. Bits are
    // processed from bit 0 up, words from right to left.
    const bool ife = (bltcon1_ & 0x08) != 0;
    const bool efe = (bltcon1_ & 0x10) != 0;
    const bool fill = desc && (ife || efe);
    const bool fci = (bltcon1_ & 0x04) != 0;

    for (int y = 0; y < height; y++) {
        uint32_t a_hold = 0, b_hold = 0;
        bool carry = fci;
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

            uint16_t d = minterm(a_shifted, b_shifted, c, mt);
            if (fill) {
                uint16_t out = 0;
                for (int bit = 0; bit < 16; bit++) {
                    const bool in = (d >> bit) & 1;
                    carry = carry != in;
                    if (efe ? carry : (carry || in)) out = uint16_t(out | (1u << bit));
                }
                d = out;
            }
            if (d) bzero_ = false;
            if (used) {
                deferred.push_back({dpt, d});
                dpt = uint32_t(int32_t(dpt) + delta);
            }
        }
        // In descending mode the modulos are subtracted, like the word step.
        const int msign = desc ? -1 : 1;
        apt = uint32_t(int32_t(apt) + msign * bltamod_);
        bpt = uint32_t(int32_t(bpt) + msign * bltbmod_);
        cpt = uint32_t(int32_t(cpt) + msign * bltcmod_);
        dpt = uint32_t(int32_t(dpt) + msign * bltdmod_);
    }
    for (const auto& w : deferred) chip_write(w.first, w.second);
    bltapt_ = apt;
    bltbpt_ = bpt;
    bltcpt_ = cpt;
    bltdpt_ = dpt;
}

void AmigaChipset::disk_dma() {
    disk_dma_count_++;
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
        disk_dma_empty_++;
        intreq_ = uint16_t(intreq_ | 0x0002);
        dsklen_ = uint16_t(dsklen_ & 0x7FFF);
        return;
    }
    int start = 0;
    if (adkcon_ & 0x0100) {
        for (int i = 0; i < int(mfm.size()); i++) {
            if (mfm[size_t(i)] == dsksync_) {
                intreq_ = uint16_t(intreq_ | 0x1000);
                break;
            }
        }
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

void AmigaChipset::audio_fetch(int ch) {
    AudioChannel& a = aud_[size_t(ch)];
    a.dat = chip_read(a.pt);
    a.pt += 2;
    if (--a.words_left == 0) {
        // Block done: the location/length latches reload and the channel
        // interrupt tells the program it may queue the next block.
        a.pt = a.lc;
        a.words_left = a.len ? a.len : 0x10000u;
        intreq_ = uint16_t(intreq_ | (0x0080 << ch));
        aud_irq_ = true;
    }
}

void AmigaChipset::audio_dma_changed(uint16_t old_dmacon) {
    for (int ch = 0; ch < 4; ch++) {
        const uint16_t bit = uint16_t(1 << ch);
        const bool was = (old_dmacon & kDmaen) && (old_dmacon & bit);
        const bool now = (dmacon_ & kDmaen) && (dmacon_ & bit);
        AudioChannel& a = aud_[size_t(ch)];
        if (!was && now) {
            a.dma_on = true;
            a.manual = a.manual_next = false;
            a.pt = a.lc;
            a.words_left = a.len ? a.len : 0x10000u;
            intreq_ = uint16_t(intreq_ | (0x0080 << ch));  // first word fetched
            aud_irq_ = true;
            audio_fetch(ch);
            a.byte = 0;
            a.out = int8_t(a.dat >> 8);
            a.counter = a.per ? a.per : 65536;
        } else if (was && !now) {
            a.dma_on = false;
            a.out = 0;
        }
    }
}

bool AmigaChipset::audio_run(int cck) {
    int level = 0;
    for (const AudioChannel& a : aud_) level += int(a.out) * int(a.vol);
    aud_sum_ += int64_t(level) * cck;
    aud_cck_ += cck;
    aud_irq_ = false;
    for (int ch = 0; ch < 4; ch++) {
        AudioChannel& a = aud_[size_t(ch)];
        if (!a.dma_on && !a.manual) continue;
        a.counter -= cck;
        while (a.counter <= 0) {
            // A DMA channel cannot fetch faster than its slot allows.
            int per = a.per ? a.per : 65536;
            if (a.dma_on && per < 124) per = 124;
            a.counter += per;
            if (a.byte == 0) {
                a.byte = 1;
                a.out = int8_t(a.dat & 0xFF);
                continue;
            }
            a.byte = 0;
            if (a.dma_on) {
                audio_fetch(ch);
            } else {
                intreq_ = uint16_t(intreq_ | (0x0080 << ch));
                aud_irq_ = true;
                if (!a.manual_next) {
                    a.manual = false;  // the DAC holds; nothing more to play
                    a.out = 0;
                    break;
                }
                a.dat = a.manual_dat;
                a.manual_next = false;
            }
            a.out = int8_t(a.dat >> 8);
        }
    }
    return aud_irq_;
}

int AmigaChipset::audio_take_sample() {
    if (aud_cck_ == 0) {
        int level = 0;
        for (const AudioChannel& a : aud_) level += int(a.out) * int(a.vol);
        return level;
    }
    const int v = int(aud_sum_ / aud_cck_);
    aud_sum_ = aud_cck_ = 0;
    return v;
}

uint32_t AmigaChipset::rgb(uint16_t c) const {
    const int r = (c >> 8) & 0xF;
    const int g = (c >> 4) & 0xF;
    const int b = c & 0xF;
    return 0xFF000000u | uint32_t(r * 17) << 16 | uint32_t(g * 17) << 8 | uint32_t(b * 17);
}

void AmigaChipset::render_line(uint32_t* framebuffer, int vpos) {
    // Display window: DIWSTOP's vertical V8 is the inverse of its V7.
    const int vstart = diwstrt_ >> 8;
    const int vstop = (diwstop_ >> 8) | ((diwstop_ & 0x8000) ? 0 : 0x100);
    const int hstart = diwstrt_ & 0xFF;
    const int hstop = (diwstop_ & 0xFF) | 0x100;
    const bool vwin = vpos >= vstart && vpos < vstop;
    const bool hires = (bplcon0_ & 0x8000) != 0;
    const bool ham = (bplcon0_ & 0x0800) != 0;
    const bool dpf = (bplcon0_ & 0x0400) != 0;
    int bpu = (bplcon0_ >> 12) & 7;
    if (bpu > 6) bpu = 4;  // OCS: seven planes fetch four
    if (hires && bpu > 4) bpu = 4;

    // Bitplane DMA: DDFSTRT..DDFSTOP words per plane, then the modulo.
    // Pointers advance on every fetched line, also outside the visible area.
    const int ddfs = std::max(0x18, ddfstrt_ & 0xFC);
    const int ddfe = std::min(0xD8, ddfstop_ & 0xFC);
    int nwords = 0;
    // Fetches go in 8-cycle units: one word per plane in lores, two in hires
    // (DDFSTRT $3C / DDFSTOP $D0 in hires is 40 words).
    if (ddfe >= ddfs) nwords = (((ddfe - ddfs + 7) >> 3) + 1) * (hires ? 2 : 1);
    nwords = std::min(nwords, kMaxFetchWords);
    const bool fetch = vwin && bpu > 0 && dma(kBplen) && nwords > 0;
    if (fetch) {
        for (int p = 0; p < bpu; p++) {
            for (int i = 0; i < nwords; i++) line_words_[size_t(p)][size_t(i)] = chip_read(bplpt_[size_t(p)] + uint32_t(i * 2));
            const int16_t mod = (p & 1) ? bpl2mod_ : bpl1mod_;
            bplpt_[size_t(p)] = uint32_t(int32_t(bplpt_[size_t(p)]) + nwords * 2 + mod);
        }
    }

    const int y = vpos - kFirstLine;
    if (y < 0 || y >= kHeight) return;
    uint32_t* row = framebuffer + y * kWidth;

    // Playfield colour indices at hires resolution (two per lores pixel).
    // The first fetched pixel reaches the screen at 2*DDFSTRT+17 (lores) or
    // 2*DDFSTRT+9 (hires) in DIWSTRT units; BPLCON1 delays the odd planes
    // (PF1H) and the even planes (PF2H) by up to 15 lores pixels.
    std::array<uint8_t, kWidth * 2> pix{};
    const int sub = hires ? 2 : 1;
    if (fetch) {
        const int first = 2 * ddfs + (hires ? 9 : 17);
        for (int p = 0; p < bpu; p++) {
            const int delay = (p & 1) ? ((bplcon1_ >> 4) & 15) : (bplcon1_ & 15);
            const int origin = (kFirstHpos - (first + delay)) * sub;  // pixel offset of x = 0
            const auto& words = line_words_[size_t(p)];
            for (int xs = 0; xs < kWidth * sub; xs++) {
                const int off = origin + xs;
                if (off < 0) continue;
                const int w = off >> 4;
                if (w >= nwords) break;
                if (words[size_t(w)] & (0x8000u >> (off & 15))) pix[size_t(xs)] = uint8_t(pix[size_t(xs)] | (1 << p));
            }
        }
    }

    // Sprites for this line: the lowest-numbered sprite wins.
    std::array<uint8_t, kWidth> spr_col{};   // colour register (0 = none)
    std::array<uint8_t, kWidth> spr_pair{};  // sprite pair 0..3
    for (int s = 7; s >= 0; s--) {
        if (!spr_line_on_[size_t(s)][size_t(y)]) continue;
        if ((s & 1) && (spr_line_ctl_[size_t(s)][size_t(y)] & 0x80) && spr_line_on_[size_t(s - 1)][size_t(y)]) {
            continue;  // attached odd sprite: drawn with its even partner
        }
        const uint16_t pos = spr_line_pos_[size_t(s)][size_t(y)];
        const uint16_t ctl = spr_line_ctl_[size_t(s)][size_t(y)];
        const bool attached = !(s & 1) && (spr_line_ctl_[size_t(s + 1)][size_t(y)] & 0x80) &&
                              spr_line_on_[size_t(s + 1)][size_t(y)];
        const int x0 = (((pos & 0xFF) << 1) | (ctl & 1)) - kFirstHpos;
        const uint16_t da = spr_line_data_[size_t(s)][size_t(y)];
        const uint16_t db = spr_line_datb_[size_t(s)][size_t(y)];
        const uint16_t oa = attached ? spr_line_data_[size_t(s + 1)][size_t(y)] : 0;
        const uint16_t ob = attached ? spr_line_datb_[size_t(s + 1)][size_t(y)] : 0;
        for (int i = 0; i < 16; i++) {
            const int x = x0 + i;
            if (x < 0 || x >= kWidth) continue;
            const uint16_t m = uint16_t(0x8000u >> i);
            int idx = ((da & m) ? 1 : 0) | ((db & m) ? 2 : 0);
            if (attached) idx |= ((oa & m) ? 4 : 0) | ((ob & m) ? 8 : 0);
            if (!idx) continue;
            spr_col[size_t(x)] = uint8_t(attached ? 16 + idx : 16 + (s & ~1) * 2 + idx);
            spr_pair[size_t(x)] = uint8_t(s >> 1);
        }
    }

    // BPLCON2: a playfield with priority code P is in front of sprite pairs
    // P..3. In single-playfield mode the PF2P code applies.
    const int pf1p = bplcon2_ & 7;
    const int pf2p = (bplcon2_ >> 3) & 7;
    const bool pf2pri = (bplcon2_ & 0x40) != 0;
    const uint16_t c0 = color_[0];
    uint16_t ham_col = c0;
    for (int x = 0; x < kWidth; x++) {
        const int h = kFirstHpos + x;
        if (!vwin || h < hstart || h >= hstop) {
            row[x] = rgb(c0);
            ham_col = c0;
            continue;
        }
        int rsum = 0, gsum = 0, bsum = 0;
        bool front1 = false, front2 = false;  // non-transparent playfield pixels
        for (int k = 0; k < sub; k++) {
            const int idx = pix[size_t(x * sub + k)];
            uint16_t c;
            if (dpf) {
                const int p1 = (idx & 1) | ((idx >> 1) & 2) | ((idx >> 2) & 4);
                const int p2 = ((idx >> 1) & 1) | ((idx >> 2) & 2) | ((idx >> 3) & 4);
                if (p1) front1 = true;
                if (p2) front2 = true;
                if (pf2pri) c = p2 ? color_[size_t(8 + p2)] : p1 ? color_[size_t(p1)] : c0;
                else c = p1 ? color_[size_t(p1)] : p2 ? color_[size_t(8 + p2)] : c0;
            } else if (ham && bpu >= 5) {
                const int v = idx & 15;
                switch ((idx >> 4) & 3) {
                    case 0: ham_col = color_[size_t(v)]; break;
                    case 1: ham_col = uint16_t((ham_col & 0xFF0) | v); break;
                    case 2: ham_col = uint16_t((ham_col & 0x0FF) | (v << 8)); break;
                    default: ham_col = uint16_t((ham_col & 0xF0F) | (v << 4)); break;
                }
                c = ham_col;
                if (idx) front2 = true;
            } else {
                if (idx) front2 = true;
                if (idx >= 32) c = uint16_t((color_[size_t(idx - 32)] >> 1) & 0x777);  // EHB
                else c = color_[size_t(idx)];
            }
            rsum += (c >> 8) & 15;
            gsum += (c >> 4) & 15;
            bsum += c & 15;
        }
        const int sc = spr_col[size_t(x)];
        if (sc) {
            const int pair = spr_pair[size_t(x)];
            bool hidden;
            if (dpf) hidden = (front1 && pair >= pf1p) || (front2 && pair >= pf2p);
            else hidden = front2 && pair >= pf2p;
            if (!hidden) {
                row[x] = rgb(color_[size_t(sc)]);
                continue;
            }
        }
        const int r = rsum * 17 / sub, g = gsum * 17 / sub, b = bsum * 17 / sub;
        row[x] = 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
    }
}

void AmigaChipset::render(uint32_t*) {}

}  // namespace dsp
