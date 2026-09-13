#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Atari MARIA, the 7800's display processor. Rather than a fixed playfield
// it walks a list of display lists: a Display List List (DLL) names one
// entry per screen "zone", and each zone's Display List (DL) names the
// graphics to fetch for it. MARIA halts the CPU while it does that DMA.
class Maria {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using DliHandler = std::function<void()>;

    static constexpr int kLineRam = 160;   // cells; each covers two pixels
    static constexpr int kWidth = 320;

    void set_read_handler(ReadHandler h) { read_ = std::move(h); }
    void set_dli_handler(DliHandler h) { dli_cb_ = std::move(h); }

    void reset();
    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    // Advances one scanline of the frame. `lines` is the total for the
    // standard (262 NTSC / 312 PAL); returns true while MARIA holds the CPU.
    void scanline(int frame_scanline, int lines);
    // Emits the current line's cells into `dest` as palette indices.
    void emit(uint8_t* dest) const;

    bool dma_on() const { return dmaon_; }
    bool wsync() const { return wsync_; }
    void clear_wsync() { wsync_ = false; }
    uint8_t palette_entry(int i) const { return palette_[size_t(i) & 0x1f]; }

private:
    void draw_scanline();
    int write_line_ram(uint16_t addr, uint8_t offset, uint8_t pal);
    bool is_holey(uint16_t addr) const;
    uint8_t rd(uint16_t a) const { return read_ ? read_(a) : 0; }

    ReadHandler read_;
    DliHandler dli_cb_;

    // 32 entries indexed straight by the line-RAM cell value (palette in
    // bits 4-2, colour in bits 1-0). Entries whose low two bits are zero all
    // mirror BACKGRND, which is how the hardware lays the registers out.
    std::array<uint8_t, 32> palette_{};
    std::array<uint8_t, kLineRam> line_ram_{};
    uint16_t dpp_ = 0, dll_ = 0, dl_ = 0;
    uint16_t charbase_ = 0;
    uint8_t offset_ = 0, holey_ = 0;
    bool dli_ = false, dmaon_ = false, wsync_ = false;
    bool write_mode_ = false, cwidth_ = false, kangaroo_ = false, color_kill_ = false;
    uint8_t rm_ = 0, vblank_ = 0x80;
};

}  // namespace dsp
