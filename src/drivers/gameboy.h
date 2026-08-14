#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/lr35902.h"
#include "machine/gb_mappers.h"
#include "sound/gb_sound.h"

namespace dsp {

// Nintendo Game Boy (DMG) / Game Boy Color, ported from
// leniad/dsp-emulator src/consolas/gb.pas (+ gb_mappers.pas, gb_sound.pas).
//
//   [x] LR35902, memory map, timers, joypad, LCD regs, OAM DMA
//   [x] Frame loop 154 x 456 T @ 4.194304 MHz
//   [x] Mappers MBC1/2/3/5, HuC-1, ROM+RAM
//   [x] APU 4 channels
//   [x] DMG video (BG / Window / Sprites + priorities)
//   [x] GBC: VRAM bank 1 attrs, WRAM banks, colour CRAM, HDMA, KEY1 speed
class GameBoy : public Machine {
public:
    static constexpr int kScreenWidth = 160;
    static constexpr int kScreenHeight = 144;
    static constexpr uint32_t kCpuClock = 4194304;
    static constexpr int kScanlines = 154;
    static constexpr int kCyclesPerLine = 456;
    static constexpr int kCyclesPerFrame = kScanlines * kCyclesPerLine;
    static constexpr double kFramesPerSecond =
        double(kCpuClock) / double(kCyclesPerFrame);

    enum class Palette { Green, Grey };

    GameBoy();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override {
        // Double-speed still produces the same frame rate; the CPU simply
        // runs twice as many cycles per line.
        return kFramesPerSecond;
    }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return GBSound::kSampleRate; }

    const char* title() const override {
        return is_gbc_ ? "Game Boy Color" : "Game Boy";
    }

    bool load_boot_rom(const std::string& path, std::string* error);
    void set_palette(Palette palette);
    bool is_gbc() const { return is_gbc_; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_io(uint8_t port);
    void write_io(uint8_t port, uint8_t value);
    void on_cycles(int cycles);

    void update_timer(int cycles);
    void update_lcd_stat(int cycles_in_line);
    void do_oam_dma(uint8_t page);
    void do_hdma_block(int bytes);  // GDMA / one HDMA slice
    void render_scanline();
    void render_scanline_dmg();
    void render_scanline_gbc();
    void build_dmg_palette();

    static uint32_t rgb555_to_argb(uint16_t c);

    uint8_t bgp_shade(uint8_t color_id) const;
    uint8_t obp_shade(uint8_t palette_reg, uint8_t color_id) const;
    uint8_t read_vram(uint8_t bank, uint16_t offset) const {
        return vram_[bank & 1][offset & 0x1FFF];
    }
    void fetch_tile_line(int tile_id, bool signed_addressing, int row_in_tile,
                         uint8_t bank, uint8_t* out_lo, uint8_t* out_hi) const;
    void get_active_sprites(uint8_t* ordered, int* count, bool gbc_order) const;

    // DMG plotters
    void plot_bg_line_dmg(uint8_t* color_line, uint8_t* prio_line, int pass);
    void plot_window_line_dmg(uint8_t* color_line, uint8_t* prio_line, int pass);
    void plot_sprites_line_dmg(uint8_t* color_line, uint8_t* prio_line,
                               const uint8_t* ordered, int count, uint8_t pri_mask);

    // GBC plotters — write final ARGB into `row`, using prio_line for layering.
    // color_line stores: bits 0-1 colour id, bits 2-4 palette, bit 5 = OBJ,
    // bit 6 = BG has priority (attr bit7), bit 7 = non-zero pixel.
    void plot_bg_line_gbc(uint32_t* row, uint8_t* prio_line, int pass);
    void plot_window_line_gbc(uint32_t* row, uint8_t* prio_line, int pass);
    void plot_sprites_line_gbc(uint32_t* row, uint8_t* prio_line,
                               const uint8_t* ordered, int count, uint8_t pri_mask);

    bool load_cartridge(const std::string& path, std::string* error);
    void apply_cart_header();

    LR35902 cpu_;
    GbMapper mapper_;
    GBSound apu_;

    std::array<uint8_t, 0x100> bios_rom_{};
    std::array<std::array<uint8_t, 0x2000>, 2> vram_{};
    std::array<std::array<uint8_t, 0x1000>, 8> wram_{};
    std::array<uint8_t, 0x100> oam_{};
    std::array<uint8_t, 0x100> hram_{};
    std::array<uint8_t, 0x80> io_{};

    uint8_t lcd_control_ = 0x91;
    uint8_t stat_ = 0x85;
    uint8_t scroll_y_ = 0;
    uint8_t scroll_x_ = 0;
    uint8_t ly_ = 0;
    uint8_t lyc_ = 0;
    uint8_t window_y_ = 0;
    uint8_t window_x_ = 0;
    uint8_t bgp_ = 0xFC;
    uint8_t obp0_ = 0xFF;
    uint8_t obp1_ = 0xFF;
    bool lcd_enabled_ = true;
    bool bios_enabled_ = false;

    uint8_t div_ = 0;
    uint8_t tima_ = 0;
    uint8_t tma_ = 0;
    uint8_t tac_ = 0;
    int div_counter_ = 0;
    int tima_counter_ = 0;

    uint8_t joy_select_ = 0xCF;
    uint8_t joy_buttons_ = 0xFF;

    bool oam_dma_ = false;
    int oam_dma_pos_ = 0;

    uint8_t vram_bank_ = 0;
    uint8_t wram_bank_ = 1;

    // GBC colour RAM (32 entries each, RGB555).
    std::array<uint16_t, 32> bgc_pal_{};
    std::array<uint16_t, 32> spc_pal_{};
    uint8_t bgcolor_index_ = 0;  // BCPS
    uint8_t spcolor_index_ = 0;  // OCPS
    bool bgcolor_inc_ = false;
    bool spcolor_inc_ = false;

    // HDMA
    uint16_t hdma_src_ = 0;
    uint16_t hdma_dst_ = 0;
    uint8_t hdma_size_ = 0xFF;  // remaining length register value
    bool hdma_active_ = false;  // HBlank DMA in progress

    bool is_gbc_ = false;
    Palette palette_kind_ = Palette::Green;
    std::string save_ram_path_;

    std::array<uint32_t, 12> dmg_palette_{};
    std::vector<uint32_t> framebuffer_;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;

    int line_cycles_ = 0;
    uint8_t window_y_draw_ = 0;
    std::array<uint8_t, 160> line_prio_{};
};

}  // namespace dsp
