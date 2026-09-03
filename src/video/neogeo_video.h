#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

// LSPC2-A2 video: 381 chained sprites, an 8x8 fix layer, two palette banks
// and the raster / watchdog timer. Palette words are the NeoGeo dark+RGB555
// packing (R0/G0/B0 in bits 14-12, not linear xRGB).
class NeoGeoVideo {
public:
    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 264;
    // MAME NEOGEO_VBSTART: IRQ1 and the 224-line visarea end at line 240.
    static constexpr int kVblankLine = 240;
    static constexpr int kVisibleTop = 16;
    static constexpr int kSpriteCount = 381;
    static constexpr int kSpritesPerLine = 96;
    static constexpr int kVramWords = 0x10000;
    static constexpr int kPaletteWords = 0x1000;

    NeoGeoVideo();

    void reset();

    uint16_t read_register(uint32_t address) const;
    void write_register(uint32_t address, uint16_t value);

    uint16_t read_palette(uint32_t address) const;
    void write_palette(uint32_t address, uint16_t value);

    void set_palette_bank(int bank) { palette_bank_ = bank & 1; }
    int palette_bank() const { return palette_bank_; }

    void set_use_bios_fix(bool value) { use_bios_fix_ = value; }
    bool use_bios_fix() const { return use_bios_fix_; }

    void set_fix_roms(const uint8_t* cart, size_t cart_size, const uint8_t* bios, size_t bios_size);
    void set_sprite_rom(const uint8_t* data, size_t size);
    void set_lo_rom(const uint8_t* data, size_t size);

    void decode_graphics();

    void ack_irq(uint8_t mask);
    bool irq_vblank() const { return irq_vblank_; }
    bool irq_timer() const { return irq_timer_; }
    bool irq_reset() const { return irq_reset_; }

    void begin_frame();
    void end_scanline(int line);
    void render_frame(uint32_t* framebuffer);

    uint16_t vram(int address) const { return vram_[size_t(address) & 0xffff]; }
    void set_vram(int address, uint16_t value) { vram_[size_t(address) & 0xffff] = value; }
    uint16_t palette(int index) const {
        return palette_[size_t(palette_bank_)][size_t(index) & 0xfff];
    }
    int auto_anim() const { return auto_anim_; }
    int scanline() const { return scanline_; }

    static uint32_t colour(uint16_t packed);

private:
    void write_vram(uint16_t value);
    uint16_t read_vram() const;
    uint16_t vram_offset() const;
    void step_vram_address();
    void rebuild_palette_entry(int bank, int index);
    const uint8_t* fix_tile(int code) const;
    const uint8_t* sprite_tile(int code) const;
    void draw_fix(uint32_t* framebuffer);
    void draw_sprites(uint32_t* framebuffer);
    void draw_sprite_pixels(uint32_t* framebuffer, int dest_y, int x, int src_y, int xzoom,
                            bool flip_x, int palette, int code);
    void tick_timer();
    static bool sprite_on_scanline(int scanline, int y, int rows);

    std::array<uint16_t, kVramWords> vram_{};
    std::array<std::array<uint16_t, kPaletteWords>, 2> palette_{};
    std::array<std::array<uint32_t, kPaletteWords>, 2> palette_rgb_{};

    uint16_t vram_addr_ = 0;
    uint16_t vram_mod_ = 1;
    uint16_t vram_read_buffer_ = 0;
    uint16_t lspc_mode_ = 0;
    uint32_t timer_reload_ = 0;
    uint32_t timer_counter_ = 0;
    bool timer_enable_ = false;
    int timer_mode_ = 0;

    bool irq_vblank_ = false;
    bool irq_timer_ = false;
    bool irq_reset_ = true;

    int palette_bank_ = 0;
    bool use_bios_fix_ = true;
    int auto_anim_ = 0;
    int auto_anim_speed_ = 0;
    int auto_anim_frame_ = 0;
    int scanline_ = 0;

    std::vector<uint8_t> cart_fix_;
    std::vector<uint8_t> bios_fix_;
    std::vector<uint8_t> sprite_rom_;
    std::vector<uint8_t> lo_rom_;
    std::vector<uint8_t> fix_pixels_;       // 8x8x4bpp, cart then optional bios overlay
    std::vector<uint8_t> bios_fix_pixels_;
    std::vector<uint8_t> sprite_pixels_;    // 16x16x4bpp
    int cart_fix_tiles_ = 0;
    int bios_fix_tiles_ = 0;
    int sprite_tiles_ = 0;
};

}  // namespace dsp
