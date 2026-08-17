#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Ricoh 2C02 PPU, ported from nes_ppu.pas.
// The original draws through gfx_engine's punbuf/putpixel/actualiza_trozo
// compositing; this writes one ARGB8888 scanline at a time with the same
// layer order (backdrop, sprites-behind, background, sprites-front), the
// same sprite-0 / overflow rules, and the same loopy VRAM address updates.
class NesPpu {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr int kScanlines = 262;
    static constexpr double kFramesPerSecond = 60.0988;
    static constexpr uint32_t kClock = 1789773;
    // Same formula as nes_ppu.pas (note the 1789733 vs CPU 1789773).
    static constexpr double kPpuPixelTiming = 1789733.0 / 60.0988 / 262.0 / 256.0;

    enum Mirror : uint8_t {
        Horizontal = 1,
        Vertical = 2,
        Low = 3,
        High = 4,
        FourScreen = 5,
        Map95 = 6,
        Map243 = 7,
        Map139 = 8,
    };

    NesPpu();

    void reset();
    void draw_linea(int line, uint32_t* out);
    uint8_t read();
    void write(uint8_t value);
    void end_y_coarse();
    void dma_spr(uint8_t page, const uint8_t* cpu_mem, std::function<void(int)> steal);

    uint8_t read_mem(uint16_t address) const;
    uint8_t chr_read(uint16_t address) const;
    void chr_write(uint16_t address, uint8_t value);

    void set_line_ack(std::function<void(bool)> fn) { line_ack_ = std::move(fn); }
    void set_ppu_read(std::function<void(uint16_t)> fn) { ppu_read_ = std::move(fn); }
    void set_chr_map(const uint8_t* map) { chr_map_ = map; }

    uint8_t* chr_bank(int bank) { return chr_[size_t(bank)].data(); }
    const uint8_t* chr_bank(int bank) const { return chr_[size_t(bank)].data(); }
    uint8_t* name_table(int table) { return name_table_[size_t(table)].data(); }
    uint8_t* sprite_ram() { return sprite_ram_.data(); }
    const uint8_t* sprite_ram() const { return sprite_ram_.data(); }

    int linea = 0;
    uint16_t address = 0;
    uint16_t address_temp = 0;
    bool disable_chr = false;
    bool sprite_over_flow = false;
    bool sprite0_hit = false;
    bool dir_first = false;
    bool write_chr = false;
    uint8_t mirror = Vertical;
    uint8_t tile_x_offset = 0;
    uint8_t pos_bg = 0;
    uint8_t pos_spt = 1;
    uint8_t sprite_size = 8;
    uint8_t control1 = 0;
    uint8_t control2 = 0;
    uint8_t status = 0;
    uint8_t open_bus = 0;
    uint8_t sprite_ram_pos = 0;
    uint8_t pal_mask = 0x3f;

private:
    uint32_t set_emphasis(uint32_t color) const;
    uint32_t pal_color(uint8_t index) const;
    void put_sprites(int line, uint8_t pri, uint32_t* out);
    void put_background(uint32_t* scratch);
    void sprite_line_overflow(int line);
    int nametable_index(uint16_t address) const;
    void advance_vram();

    std::array<uint8_t, 0x100> sprite_ram_{};
    std::array<std::array<uint8_t, 0x1000>, 4> chr_{};
    std::array<std::array<uint8_t, 0x400>, 4> name_table_{};
    std::array<uint8_t, 0x20> pal_ram_{};
    std::array<uint8_t, 34 * 8> dot_line_trans_{};
    uint8_t buffer_read_ = 0;
    const uint8_t* chr_map_ = nullptr;
    std::function<void(bool)> line_ack_;
    std::function<void(uint16_t)> ppu_read_;
    std::array<uint32_t, 64> palette_{};

    static const uint8_t kMirrorTypes[9][4];
};

}  // namespace dsp
