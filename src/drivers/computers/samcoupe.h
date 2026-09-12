#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/sam_disk.h"
#include "machine/wd1772.h"
#include "sound/saa1099.h"

namespace dsp {

// SAM Coupe (Miles Gordon Technology, 1989).
// Z80B @ 6MHz, custom ASIC for video/memory paging, Philips SAA1099 sound,
// VL-1772-02 (WD1772-compatible) floppy controller.
//
// Memory: 32 pages of 16K (512K max), addressed by the Z80's 64K space split
// into four fixed 16K sections A(0000)/B(4000)/C(8000)/D(C000). LMPR (port
// 250) controls section A's page and, via bits 5/6, whether ROM0 shows
// through section A and/or ROM1 through section D; section B always mirrors
// (LMPR page + 1). HMPR (port 251) controls section C's page; section D
// mirrors (HMPR page + 1) unless ROM1 is paged in instead. VMPR (port 252)
// selects the video page and one of four screen modes.
class SamCoupe : public Machine {
public:
    static constexpr uint32_t kClock = 6000000;
    static constexpr int kTstatesPerLine = 384;
    static constexpr int kLinesPerFrame = 312;
    static constexpr int kTstatesPerFrame = kTstatesPerLine * kLinesPerFrame;  // 119808
    static constexpr double kFps = double(kClock) / double(kTstatesPerFrame);  // ~50.08
    static constexpr int kTopBorderLines = 68;
    static constexpr int kScreenLines = 192;
    static constexpr int kBottomBorderLines = kLinesPerFrame - kScreenLines - kTopBorderLines;  // 52
    static constexpr int kSideBorderCells = 8;   // 8 cells (64 T-states) each side
    static constexpr int kScreenCells = 32;
    static constexpr int kWidthCells = kScreenCells + 2 * kSideBorderCells;  // 48
    static constexpr int kScreenWidth = kWidthCells * 16;   // 768 (16 px/cell at full ASIC res)
    static constexpr int kScreenHeight = kLinesPerFrame;    // 312
    static constexpr int kNumPages = 32;                    // 512K
    static constexpr int kSampleRate = Saa1099::kSampleRate;
    // The SAA1099's own datasheet-typical clock on the SAM Coupe (derived
    // from the same 8 MHz reference used for the disk interface).
    static constexpr uint32_t kSaaClock = 8000000;

    SamCoupe();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "SAM Coupe"; }
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;
    // Bypasses the keyboard-driven BASIC "BOOT" command entirely: reads
    // track 4/side 0/sector 1 of the loaded disk straight into a RAM page
    // mapped at 0x8000 (mirroring what the ROM's own BOOT routine does
    // internally) and jumps to its entry point. Returns false if no disk
    // is loaded or that sector can't be found.
    bool auto_boot();

private:
    uint8_t mem_read(uint16_t addr);
    void mem_write(uint16_t addr, uint8_t value);
    uint8_t io_in(uint16_t port);
    void io_out(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    void update_paging();
    void out_lmpr(uint8_t value);
    void out_hmpr(uint8_t value);
    void out_vmpr(uint8_t value);
    void out_border(uint8_t value);
    void out_clut(int index, uint8_t value);
    void apply_keyboard(const MachineInputs& in);
    void update_clut_rgb(int index);
    // Returns the WD1772 register offset (0-3, after selecting the right
    // disk/side) for a disk port, or -1 if `port` isn't a disk port.
    int select_disk_port(uint16_t port);

    void render_line(int line);
    void render_mode1(uint32_t* dst, const uint8_t* page, int y) const;
    void render_mode2(uint32_t* dst, const uint8_t* page, int y) const;
    // Modes 3/4 can span more than one 16K RAM page (mode 4's 512x192 at
    // 4 bits/pixel needs 48K = 3 pages), so unlike modes 1/2 they take the
    // starting page number and fetch bytes via video_byte() rather than a
    // single flat pointer.
    void render_mode3(uint32_t* dst, int base_page, int y) const;
    void render_mode4(uint32_t* dst, int base_page, int y) const;
    uint8_t video_byte(int base_page, uint32_t offset) const;

    int visible_screen_page() const;  // even page currently selected by VMPR
    int screen_mode() const;          // 1-4

    Z80 cpu_;
    Saa1099 saa_;
    SamDisk disk1_, disk2_;
    Wd1772 fdc_;

    // 32 pages of 16K RAM, plus 2 pages (ROM0/ROM1) of 16K ROM.
    std::array<std::array<uint8_t, 0x4000>, kNumPages> ram_{};
    std::array<uint8_t, 0x4000> rom0_{};
    std::array<uint8_t, 0x4000> rom1_{};

    uint8_t lmpr_ = 0, hmpr_ = 0, vmpr_ = 0;
    uint8_t border_ = 0;
    uint8_t status_ = 0xff;   // active-low interrupt flags, read via port 249
    uint8_t line_int_ = 0xff; // port 249 write: target line for the line interrupt

    // Effective page number for each 16K section, or -1/-2 sentinels for ROM.
    static constexpr int kSectRom0 = -1;
    static constexpr int kSectRom1 = -2;
    std::array<int, 4> section_page_{kSectRom0, 1, 0, 1};

    uint8_t* section_ptr(int slot);

    // Colour look-up table: 16 entries, each a 7-bit ASIC value plus the
    // decoded ARGB8888 colour.
    std::array<uint8_t, 16> clut_{};
    std::array<uint32_t, 16> clut_rgb_{};

    // 9-column x 8-row key matrix (columns 0-7 = address lines A8-A15,
    // column 8 = RDMSEL / mouse). Active low, matches the real hardware.
    std::array<uint8_t, 9> key_matrix_{};

    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};

    bool flash_phase_ = false;
    int flash_count_ = 0;

    int line_ = 0;
    int t_in_line_ = 0;
    int frame_t_ = 0;

    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
};

}  // namespace dsp
