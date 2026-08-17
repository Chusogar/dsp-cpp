#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/rom_loader.h"
#include "video/gfx.h"

namespace dsp {

void build_s16_palette_luts(uint8_t normal[32], uint8_t shadow[32], uint8_t hilight[32]);
uint32_t s16_argb(uint8_t r, uint8_t g, uint8_t b);
uint32_t s16_mix_shadow(uint32_t dest, uint32_t shadow);

bool load_roms16w(RomLoader& loader, const std::vector<RomEntry>& entries,
                  std::vector<uint16_t>& dest, std::string* error);
bool load_roms16b(RomLoader& loader, const std::vector<RomEntry>& entries,
                  std::vector<uint16_t>& dest, std::string* error);
bool load_roms32dw(RomLoader& loader, const std::vector<RomEntry>& entries,
                   std::vector<uint32_t>& dest, std::string* error);
bool load_rom_bytes(RomLoader& loader, const std::vector<RomEntry>& entries,
                    std::vector<uint8_t>& dest, std::string* error);

void decode_s16_tiles(GfxSet& tiles, const std::vector<uint8_t>& rom, int n);

struct Sega16Video {
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 224;
    static constexpr int kMapWidth = 1024;
    static constexpr int kMapHeight = 512;
    static constexpr int kTextWidth = 512;

    void reset();
    void init_palette_luts();
    void set_palette_entry(int index, uint16_t value, bool split_shadow);
    void mark_tile(uint16_t word_offset);
    void apply_screen_select_16b(uint16_t char_offset);   // $740 / $741, 4-bit
    void apply_screen_select_16a(uint16_t char_offset);   // $74e / $74f, 3-bit
    void apply_screen_select_hangon(uint16_t char_offset);  // $74e / $74f, 2-bit

    void render_tile_pages(std::vector<uint32_t>& low, std::vector<uint32_t>& high, int first_page,
                           bool transparent, int color_shift, int code_mask, int pri_mask,
                           bool use_tile_bank, bool extra_code_bit);
    void render_text(std::vector<uint32_t>& low, std::vector<uint32_t>& high, int color_shift,
                     int code_mask, int pri_mask, bool use_tile_bank);

    void blit_scrolled(uint32_t* dest, const std::vector<uint32_t>& source, int scroll_x,
                       int scroll_y, int src_width, int src_height) const;
    void blit_text(uint32_t* dest, const std::vector<uint32_t>& source) const;

    GfxSet tiles;
    std::array<uint8_t, 32> normal{};
    std::array<uint8_t, 32> shadow{};
    std::array<uint8_t, 32> hilight{};
    std::array<uint32_t, 0x2001> palette{};
    std::array<uint16_t, 0x1000> pal_ram{};
    std::array<uint16_t, 0x8000> tile_ram{};
    std::array<uint16_t, 0x800> char_ram{};
    std::array<uint16_t, 0x800> sprite_ram{};
    std::array<std::array<bool, 0x800>, 8> tile_dirty{};
    std::array<bool, 0x800> text_dirty{};
    std::array<uint8_t, 8> screens{};
    std::array<uint8_t, 2> tile_bank{};
    std::array<uint8_t, 16> sprite_bank{};
    bool screen_enabled = true;
    uint8_t tile_banks = 0;
};

void draw_sprites_16a(Sega16Video& video, uint32_t* dest, const std::vector<uint16_t>& sprite_rom,
                      int banks, int pri, int pal_base, uint32_t shadow_index);
void draw_sprites_hangon(Sega16Video& video, uint32_t* dest, const std::vector<uint16_t>& sprite_rom,
                         const std::vector<uint8_t>& zoom, int banks, int pri);
void draw_sprites_16b(Sega16Video& video, uint32_t* dest, const std::vector<uint16_t>& sprite_rom,
                      int banks, int pri, uint32_t shadow_index);
void draw_sprites_outrun(Sega16Video& video, uint32_t* dest, const std::vector<uint32_t>& sprite_rom,
                         int banks, int pri, uint32_t shadow_index);

void decode_outrun_road(std::vector<uint8_t>& road_gfx, const std::vector<uint8_t>& rom);
void decode_hangon_road(std::vector<uint8_t>& road_gfx, const std::vector<uint8_t>& rom);
void draw_outrun_road(uint32_t* dest, const uint32_t* palette, const uint16_t* buffer,
                      const uint8_t* road_gfx, uint8_t control, uint16_t colorbase1,
                      uint16_t colorbase2, uint16_t colorbase3, uint16_t xoff, int pri);
void draw_hangon_road(uint32_t* dest, const uint32_t* palette, const uint16_t* road_ram,
                      const uint8_t* road_gfx, uint16_t colorbase1, uint16_t colorbase2, int pri);

}  // namespace dsp
