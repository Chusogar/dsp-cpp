#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// OCS Agnus/Denise/Paula: copper, blitter, bitplanes, INTENA/DMACON, disk DMA.
class AmigaChipset {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 256;

    using ChipRead16 = std::function<uint16_t(uint32_t)>;
    using ChipWrite16 = std::function<void(uint32_t, uint16_t)>;
    using TrackMfm = std::function<std::vector<uint16_t>()>;

    void set_chip_handlers(ChipRead16 read, ChipWrite16 write);
    void set_track_mfm(TrackMfm mfm) { track_mfm_ = std::move(mfm); }

    void reset();
    uint16_t read(uint16_t reg);
    void write(uint16_t reg, uint16_t value);

    void begin_frame();
    void set_vpos(int vpos) { vpos_ = vpos; }
    void copper_line(int vpos);
    void render(uint32_t* framebuffer);

    void set_ciaa_irq(bool v) { ciaa_irq_ = v; }
    void set_ciab_irq(bool v) { ciab_irq_ = v; }

    int ipl() const;
    uint16_t intena() const { return intena_; }
    uint16_t intreq() const { return intreq_; }
    uint16_t dmacon() const { return dmacon_; }
    uint16_t color00() const { return color_[0]; }
    uint16_t bplcon0() const { return bplcon0_; }
    uint32_t cop1lc() const { return cop1lc_; }
    uint32_t cop2lc() const { return cop2lc_; }
    uint32_t bplpt0() const { return bplpt_[0]; }
    uint16_t color(int i) const { return color_[size_t(i) & 31]; }
    uint32_t sprpt0() const { return sprpt_[0]; }
    uint16_t sprpos0() const { return sprpos_[0]; }
    uint16_t sprctl0() const { return sprctl_[0]; }

    bool dma_master() const { return (dmacon_ & 0x0200) != 0; }

private:
    uint16_t chip_read(uint32_t addr) const;
    void chip_write(uint32_t addr, uint16_t value);
    void poke_ptr(uint32_t& p, bool high, uint16_t value);
    static void setclr(uint16_t& reg, uint16_t value, uint16_t mask);
    bool dma(uint16_t bit) const { return (dmacon_ & 0x0200) && (dmacon_ & bit); }
    void copper_restart();
    void copper_step_until_wait(int vpos);
    void sprite_dma_line(int vpos);
    void blit();
    void disk_dma();
    uint32_t rgb(uint16_t c) const;
    void plot_sprites(uint32_t* framebuffer) const;

    ChipRead16 read16_;
    ChipWrite16 write16_;
    TrackMfm track_mfm_;

    uint16_t dmacon_ = 0;
    uint16_t intena_ = 0;
    uint16_t intreq_ = 0;
    uint16_t adkcon_ = 0;
    uint16_t dsklen_ = 0;
    uint16_t dsksync_ = 0x4489;
    uint32_t dskpt_ = 0;
    uint32_t cop1lc_ = 0, cop2lc_ = 0, coppc_ = 0;
    uint16_t diwstrt_ = 0x2C81, diwstop_ = 0xF4C1;
    uint16_t ddfstrt_ = 0x0038, ddfstop_ = 0x00D0;
    uint16_t bplcon0_ = 0, bplcon1_ = 0, bplcon2_ = 0;
    int16_t bpl1mod_ = 0, bpl2mod_ = 0;
    std::array<uint32_t, 6> bplpt_{};
    std::array<uint16_t, 32> color_{};

    uint16_t bltcon0_ = 0, bltcon1_ = 0;
    uint16_t bltafwm_ = 0xFFFF, bltalwm_ = 0xFFFF;
    uint32_t bltapt_ = 0, bltbpt_ = 0, bltcpt_ = 0, bltdpt_ = 0;
    int16_t bltamod_ = 0, bltbmod_ = 0, bltcmod_ = 0, bltdmod_ = 0;
    uint16_t bltadat_ = 0, bltbdat_ = 0, bltcdat_ = 0;
    uint16_t bltsize_ = 0;
    bool bzero_ = true;

    int vpos_ = 0;
    bool lof_ = false;
    bool ciaa_irq_ = false;
    bool ciab_irq_ = false;
    bool cop_stopped_ = true;
    bool copper_active_ = false;

    std::array<uint32_t, 8> sprpt_{};
    std::array<uint16_t, 8> sprpos_{};
    std::array<uint16_t, 8> sprctl_{};
    std::array<uint16_t, 8> sprdata_{};
    std::array<uint16_t, 8> sprdatb_{};
    // Per visible line, last DMA data so render() can composite sprites.
    std::array<std::array<uint16_t, kHeight>, 8> spr_line_data_{};
    std::array<std::array<uint16_t, kHeight>, 8> spr_line_datb_{};
    std::array<std::array<uint16_t, kHeight>, 8> spr_line_pos_{};
    std::array<std::array<uint16_t, kHeight>, 8> spr_line_ctl_{};
    std::array<std::array<uint8_t, kHeight>, 8> spr_line_on_{};
};

}  // namespace dsp
