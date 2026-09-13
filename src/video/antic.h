#pragma once

#include <cstdint>
#include <functional>

#include "video/gtia.h"

namespace dsp {

// Atari ANTIC (C012296): a small coprocessor that walks a "display list"
// program in RAM, fetches character/bitmap data by DMA and streams
// playfield colour codes to GTIA. It also owns the vertical-blank and
// display-list interrupts, the character set and player/missile base
// pointers, and the fine-scroll registers.
class Antic {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using NmiHandler = std::function<void()>;

    // NTSC: 262 scanlines of 114 CPU cycles each, ~59.92 Hz.
    static constexpr int kLinesPerFrame = 262;
    static constexpr int kCyclesPerLine = 114;
    // The visible window, measured in colour clocks. A wide playfield is
    // 192 clocks and starts at clock 24, so that window covers every mode.
    static constexpr int kFirstVisibleClock = 24;
    static constexpr int kVisibleClocks = 192;
    // Two screen pixels per colour clock, so a hi-res (mode F) pixel maps
    // exactly onto one screen pixel.
    static constexpr int kScreenWidth = kVisibleClocks * 2;
    static constexpr int kFirstVisibleLine = 8;
    static constexpr int kScreenHeight = 240;

    Antic(Gtia& gtia);

    void reset();
    void set_memory_handler(ReadHandler h) { read_ = std::move(h); }
    void set_nmi_handler(NmiHandler h) { nmi_ = std::move(h); }

    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    void begin_frame();
    // Advances one scanline: runs the display-list state machine, raises
    // any interrupt due, and renders into `dst` (kScreenWidth palette
    // indices) when the line is on screen. Returns how many CPU cycles
    // this line's DMA stole.
    int scanline(int line, uint32_t* dst);

    bool wsync_pending() const { return wsync_; }
    void clear_wsync() { wsync_ = false; }

    int vcount() const { return vcount_; }
    uint8_t debug_dmactl() const { return dmactl_; }
    uint8_t debug_chbase() const { return chbase_; }
    uint16_t debug_dlist() const { return dlist_; }
    uint8_t debug_nmien() const { return nmien_; }

private:
    void fetch_display_list(int line);
    void render_line(uint32_t* dst);
    uint8_t mem(uint16_t a) const { return read_ ? read_(a) : 0; }
    void do_pm_dma(int line);

    Gtia& gtia_;
    ReadHandler read_;
    NmiHandler nmi_;

    uint8_t dmactl_ = 0, chactl_ = 0, hscrol_ = 0, vscrol_ = 0;
    uint8_t pmbase_ = 0, chbase_ = 0, nmien_ = 0, nmist_ = 0;
    uint16_t dlist_ = 0;

    uint16_t dl_ptr_ = 0;      // current display-list fetch address
    uint16_t scan_addr_ = 0;   // current screen-data address (LMS target)
    uint8_t mode_ = 0;         // ANTIC mode of the current display block
    int mode_line_ = 0;        // scanline within the current mode block
    int mode_height_ = 0;      // total scanlines in the current mode block
    bool dli_pending_ = false; // this block's last line raises a DLI
    bool hscroll_ = false, vscroll_ = false;
    bool list_done_ = false;   // hit a JVB, wait for vertical blank
    int vcount_ = 0;
    bool wsync_ = false;
};

}  // namespace dsp
