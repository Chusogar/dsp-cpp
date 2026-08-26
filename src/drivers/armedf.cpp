#include "drivers/armedf.h"

#include "core/rom_loader.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint32_t kTransparent = 0;

uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t(n | (n << 4));
}

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    return loader.load(entries, dest, error);
}

// Interleaved byte ROMs for 68000 (offset 0 = even, 1 = odd).
bool load_16b(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> temp(entry.length);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, temp, error)) return false;
        const size_t need = size_t(entry.offset) + size_t(entry.length) * 2;
        if (dest.size() < need) dest.resize(need, 0);
        for (uint32_t i = 0; i < entry.length; i++)
            dest[size_t(entry.offset) + size_t(i) * 2] = temp[i];
    }
    return true;
}

GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    // gfx_set_desc_data(4,0,32*8,0,1,2,3) + pf_x
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {4, 0, 12, 8, 20, 16, 28, 24};
    layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32};
    return layout;
}

GfxLayout tile_layout(int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 128 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {4, 0, 12, 8, 20, 16, 28, 24, 32 + 4, 32 + 0, 32 + 12, 32 + 8,
                        32 + 20, 32 + 16, 32 + 28, 32 + 24};
    layout.y_offsets = {0 * 64,  1 * 64,  2 * 64,  3 * 64,  4 * 64,  5 * 64,  6 * 64,  7 * 64,
                        8 * 64,  9 * 64,  10 * 64, 11 * 64, 12 * 64, 13 * 64, 14 * 64, 15 * 64};
    return layout;
}

// Armed F / Cclimbr2 sprite layout (ps_x with $800*64*8 plane pair).
GfxLayout sprite_layout_armedf(int total) {
    const int bank = 0x800 * 64 * 8;
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {4, 0, bank + 4, bank + 0, 12, 8, bank + 12, bank + 8,
                        20, 16, bank + 20, bank + 16, 28, 24, bank + 28, bank + 24};
    layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32,
                        8 * 32, 9 * 32, 10 * 32, 11 * 32, 12 * 32, 13 * 32, 14 * 32, 15 * 32};
    return layout;
}

void blit_trans(std::vector<uint32_t>& dest, int dest_w, int dest_h, int dx, int dy,
                const uint8_t* pixels, int pw, int ph, const uint32_t* palette, int color_base,
                bool flipx, bool flipy, int transparent_index = 15) {
    for (int y = 0; y < ph; y++) {
        const int sy = dy + y;
        if (sy < 0 || sy >= dest_h) continue;
        const int src_y = flipy ? (ph - 1 - y) : y;
        for (int x = 0; x < pw; x++) {
            const int sx = dx + x;
            if (sx < 0 || sx >= dest_w) continue;
            const int src_x = flipx ? (pw - 1 - x) : x;
            const uint8_t pix = pixels[src_y * pw + src_x];
            if (int(pix) == transparent_index) continue;
            dest[size_t(sy * dest_w + sx)] = palette[size_t(color_base + pix)];
        }
    }
}

void blit_opaque(std::vector<uint32_t>& dest, int dest_w, int dest_h, int dx, int dy,
                 const uint8_t* pixels, int pw, int ph, const uint32_t* palette, int color_base,
                 bool flipx, bool flipy) {
    for (int y = 0; y < ph; y++) {
        const int sy = dy + y;
        if (sy < 0 || sy >= dest_h) continue;
        const int src_y = flipy ? (ph - 1 - y) : y;
        for (int x = 0; x < pw; x++) {
            const int sx = dx + x;
            if (sx < 0 || sx >= dest_w) continue;
            const int src_x = flipx ? (pw - 1 - x) : x;
            const uint8_t pix = pixels[src_y * pw + src_x];
            dest[size_t(sy * dest_w + sx)] = palette[size_t(color_base + pix)];
        }
    }
}

}  // namespace

ArmedF::ArmedF(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      dac0_(1.0f),
      dac1_(1.0f),
      layer_txt_op_(512 * 256, 0),
      layer_txt_tr_(512 * 256, 0),
      layer_bg_(1024 * 512, 0),
      layer_fg_(1024 * 512, 0),
      composite_(512 * 512, 0),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    main_cpu_.set_memory_handlers(
        [this](uint32_t a) { return main_read_word(a); },
        [this](uint32_t a, uint16_t v) { main_write_word(a, v); });
    main_cpu_.set_byte_handlers(
        [this](uint32_t a) { return main_read_byte(a); },
        [this](uint32_t a, uint8_t v) { main_write_byte(a, v); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers(
        [this](uint16_t p) { return sound_in(p); },
        [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });

    // Per-game parameters from iniciar_armedf.
    switch (game_) {
        case Game::ArmedF:
            irq_level_ = 1;
            sprite_offset_ = 0x80;
            sprite_num_ = 0x1ff;
            rotate_270_ = true;  // tipo_maquina 275
            break;
        case Game::Terraf:
            irq_level_ = 1;
            sprite_offset_ = 0x80;
            sprite_num_ = 0x7f;
            break;
        case Game::Cclimbr2:
            irq_level_ = 2;
            sprite_offset_ = 0;
            sprite_num_ = 0x1ff;
            break;
        case Game::Legion:
            irq_level_ = 2;
            sprite_offset_ = 0;
            sprite_num_ = 0x7f;
            rotate_270_ = true;  // tipo_maquina 278
            break;
    }
    if (rotate_270_) {
        display_width_ = kScreenHeight;   // 240
        display_height_ = kScreenWidth;   // 320
        framebuffer_.assign(size_t(display_width_ * display_height_), 0xff000000u);
    }
}

const char* ArmedF::title() const {
    switch (game_) {
        case Game::Terraf: return "Terra Force";
        case Game::Cclimbr2: return "Crazy Climber 2";
        case Game::Legion: return "Legion";
        default: return "Armed Formation";
    }
}

bool ArmedF::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    rom_.assign(0x60000, 0);

    if (game_ == Game::ArmedF) {
        if (!load_16b(loader,
                      {{"06.3d", 0x10000, 0, 0x0f9015e2},
                       {"01.3f", 0x10000, 1, 0x816ff7c5},
                       {"07.5d", 0x10000, 0x20000, 0x5b3144a5},
                       {"02.4f", 0x10000, 0x20001, 0xfa10c29d},
                       {"af_08.rom", 0x10000, 0x40000, 0xd1d43600},
                       {"af_03.rom", 0x10000, 0x40001, 0xbbe1fe2d}},
                      rom_, error))
            return false;
        std::vector<uint8_t> snd(0x10000, 0);
        if (!load_raw(loader, {{"af_10.rom", 0x10000, 0, 0xc5eacb87}}, snd, error)) return false;
        std::copy(snd.begin(), snd.end(), mem_snd_.begin());
        gfx_char_.assign(0x8000, 0);
        if (!load_raw(loader, {{"09.11c", 0x8000, 0, 0x5c6993d5}}, gfx_char_, error)) return false;
        gfx_bg_.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"af_14.rom", 0x10000, 0, 0x8c5dc5a7},
                       {"af_13.rom", 0x10000, 0x10000, 0x136a58a3}},
                      gfx_bg_, error))
            return false;
        gfx_fg_.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"af_04.rom", 0x10000, 0, 0x44d3af4f},
                       {"af_05.rom", 0x10000, 0x10000, 0x92076cab}},
                      gfx_fg_, error))
            return false;
        gfx_spr_.assign(0x40000, 0);
        if (!load_raw(loader,
                      {{"af_11.rom", 0x20000, 0, 0xb46c473c},
                       {"af_12.rom", 0x20000, 0x20000, 0x23cb6bfe}},
                      gfx_spr_, error))
            return false;
    } else if (game_ == Game::Terraf) {
        // Terra Force (MAME terraf)
        if (!load_16b(loader,
                      {{"8.6e", 0x10000, 0, 0xfd58fa06},
                       {"3.6h", 0x10000, 1, 0x54823a7d},
                       {"7.4e", 0x10000, 0x20000, 0xfde8de7e},
                       {"2.4h", 0x10000, 0x20001, 0xdb987414},
                       {"6.3e", 0x10000, 0x40000, 0xa5bb8c3b},
                       {"1.3h", 0x10000, 0x40001, 0xd2de6d28}},
                      rom_, error))
            return false;
        std::vector<uint8_t> snd(0x10000, 0);
        if (!load_raw(loader, {{"11.17k", 0x10000, 0, 0x4407d475}}, snd, error)) return false;
        std::copy(snd.begin(), snd.end(), mem_snd_.begin());
        // NB1414M4 ROM (10.11c) — stubbed for now (text overlay MCU)
        gfx_char_.assign(0x8000, 0);
        if (!load_raw(loader, {{"9.11e", 0x8000, 0, 0xbc6f7cbc}}, gfx_char_, error)) return false;
        gfx_bg_.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"15.8a", 0x10000, 0, 0x2144d8e0},
                       {"14.6a", 0x10000, 0x10000, 0x744f5c9e}},
                      gfx_bg_, error))
            return false;
        gfx_fg_.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"5.15h", 0x10000, 0, 0x25d23dfd},
                       {"4.13h", 0x10000, 0x10000, 0xb9b0fe27}},
                      gfx_fg_, error))
            return false;
        gfx_spr_.assign(0x20000, 0);
        if (!load_raw(loader,
                      {{"12.7d", 0x10000, 0, 0x2d1f2ceb},
                       {"13.9d", 0x10000, 0x10000, 0x1d2f92d6}},
                      gfx_spr_, error))
            return false;
        dsw_a_ = 0xffcf;
        dsw_b_ = 0xff3f;
    } else {
        if (error) *error = "ROM set not implemented for this game yet";
        return false;
    }

    warnings_ = loader.warnings();
    decode_graphics();
    return true;
}

void ArmedF::decode_graphics() {
    chars_.decode(char_layout(0x400), gfx_char_);
    tiles_bg_.decode(tile_layout(0x400), gfx_bg_);
    tiles_fg_.decode(tile_layout(0x400), gfx_fg_);
    if (game_ == Game::ArmedF || game_ == Game::Cclimbr2) {
        sprites_.decode(sprite_layout_armedf(0x800), gfx_spr_);
    } else {
        // Terra Force / Legion: smaller sprite ROM, ps_x_terraf plane stride
        const int bank = 0x400 * 64 * 8;
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 0x400;
        layout.planes = 4;
        layout.char_increment = 64 * 8;
        layout.plane_offsets = {0, 1, 2, 3};
        layout.x_offsets = {4, 0, bank + 4, bank + 0, 12, 8, bank + 12, bank + 8,
                            20, 16, bank + 20, bank + 16, 28, 24, bank + 28, bank + 24};
        layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32,
                            8 * 32, 9 * 32, 10 * 32, 11 * 32, 12 * 32, 13 * 32, 14 * 32, 15 * 32};
        sprites_.decode(layout, gfx_spr_);
    }
}

bool ArmedF::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void ArmedF::set_color(int pos, uint16_t data) {
    const uint8_t r = pal4bit(uint8_t(data >> 8));
    const uint8_t g = pal4bit(uint8_t(data >> 4));
    const uint8_t b = pal4bit(uint8_t(data));
    palette_[size_t(pos & 0x7ff)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void ArmedF::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    dac0_.reset();
    dac1_.reset();
    video_reg_ = 0;
    scroll_fg_x_ = scroll_fg_y_ = 0;
    scroll_bg_x_ = scroll_bg_y_ = 0;
    sound_latch_ = 0;
    in0_ = in1_ = 0xffff;
    audio_accum_ = 0;
    sound_irq_counter_ = 0;
    audio_.clear();
    sprite_buffer_.fill(0);
    ram_.fill(0);
    ram_sprites_.fill(0);
    ram_bg_.fill(0);
    ram_fg_.fill(0);
    ram_clut_.fill(0);
    ram_txt_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    main_cpu_.set_irq(irq_level_, IrqLine::Clear);
    nb1414_.reset();
    nb1414_.set_memory(ram_txt_.data());
    frame_counter_ = 0;
    prev_video_reg_ = 0;
}

void ArmedF::set_inputs(const MachineInputs& inputs) {
    // Active-low (Pascal eventos_armedf).
    in0_ = 0xffff;
    in1_ = 0xffff;
    auto clr0 = [&](uint16_t mask) { in0_ = uint16_t(in0_ & ~mask); };
    auto clr1 = [&](uint16_t mask) { in1_ = uint16_t(in1_ & ~mask); };

    if (inputs.player1.up) clr0(0x0001);
    if (inputs.player1.down) clr0(0x0002);
    if (inputs.player1.left) clr0(0x0004);
    if (inputs.player1.right) clr0(0x0008);
    if (inputs.player1.button1) clr0(0x0010);
    if (inputs.player1.button2) clr0(0x0020);
    if (inputs.player1.button3) clr0(0x0040);
    if (inputs.player1.start) clr0(0x0100);
    if (inputs.player2.start) clr0(0x0200);
    if (inputs.coin1) clr0(0x0400);
    if (inputs.coin2) clr0(0x0800);

    if (inputs.player2.up) clr1(0x0001);
    if (inputs.player2.down) clr1(0x0002);
    if (inputs.player2.left) clr1(0x0004);
    if (inputs.player2.right) clr1(0x0008);
    if (inputs.player2.button1) clr1(0x0010);
    if (inputs.player2.button2) clr1(0x0020);
    if (inputs.player2.button3) clr1(0x0040);
}

void ArmedF::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

uint16_t ArmedF::main_read_word(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x5ffff) {
        const uint32_t a = address & ~1u;
        if (a + 1 < rom_.size())
            return uint16_t((rom_[a] << 8) | rom_[a + 1]);
        return 0xffff;
    }

    // Terra Force / Cclimbr2 / Legion map
    if (game_ != Game::ArmedF) {
        if (address >= 0x60000 && address <= 0x603ff)
            return ram_sprites_[(address & 0xfff) >> 1];
        if ((address >= 0x60400 && address <= 0x63fff) ||
            (address >= 0x6a000 && address <= 0x6a9ff)) {
            const uint32_t idx = (address - 0x60000) >> 1;
            if (idx < ram_.size()) return ram_[idx];
            return 0xffff;
        }
        if (address >= 0x64000 && address <= 0x64fff)
            return palette_ram_[(address & 0xfff) >> 1];
        if (address >= 0x68000 && address <= 0x69fff)
            return ram_txt_[(address & 0x1fff) >> 1];
        if (address >= 0x6c000 && address <= 0x6cfff)
            return ram_clut_[(address & 0xfff) >> 1];
        if (address >= 0x70000 && address <= 0x70fff)
            return ram_fg_[(address & 0xfff) >> 1];
        if (address >= 0x74000 && address <= 0x74fff)
            return ram_bg_[(address & 0xfff) >> 1];
        if (address == 0x78000) return in0_;
        if (address == 0x78002) return in1_;
        if (address == 0x78004) return dsw_a_;
        if (address == 0x78006) return dsw_b_;
        return 0xffff;
    }

    // Armed F map
    if (address >= 0x60000 && address <= 0x60fff)
        return ram_sprites_[(address & 0xfff) >> 1];
    if ((address >= 0x61000 && address <= 0x65fff) ||
        (address >= 0x6c000 && address <= 0x6c7ff)) {
        const uint32_t idx = (address - 0x60000) >> 1;
        if (idx < ram_.size()) return ram_[idx];
        return 0xffff;
    }
    if (address >= 0x66000 && address <= 0x66fff)
        return ram_bg_[(address & 0xfff) >> 1];
    if (address >= 0x67000 && address <= 0x67fff)
        return ram_fg_[(address & 0xfff) >> 1];
    if (address >= 0x68000 && address <= 0x69fff)
        return ram_txt_[(address & 0x1fff) >> 1];
    if (address >= 0x6a000 && address <= 0x6afff)
        return palette_ram_[(address & 0xfff) >> 1];
    if (address >= 0x6b000 && address <= 0x6bfff)
        return ram_clut_[(address & 0xfff) >> 1];
    if (address == 0x6c000) return in0_;
    if (address == 0x6c002) return in1_;
    if (address == 0x6c004) return dsw_a_;
    if (address == 0x6c006) return dsw_b_;
    return 0xffff;
}

void ArmedF::main_write_word(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address <= 0x5ffff) return;

    if (game_ != Game::ArmedF) {
        if (address >= 0x60000 && address <= 0x603ff) {
            ram_sprites_[(address & 0xfff) >> 1] = value;
            return;
        }
        if ((address >= 0x60400 && address <= 0x63fff) ||
            (address >= 0x6a000 && address <= 0x6a9ff)) {
            const uint32_t idx = (address - 0x60000) >> 1;
            if (idx < ram_.size()) ram_[idx] = value;
            return;
        }
        if (address >= 0x64000 && address <= 0x64fff) {
            const int pos = int((address & 0xfff) >> 1);
            if (palette_ram_[size_t(pos)] != value) {
                palette_ram_[size_t(pos)] = value;
                set_color(pos, value);
            }
            return;
        }
        if (address >= 0x68000 && address <= 0x69fff) {
            ram_txt_[(address & 0x1fff) >> 1] = uint8_t(value & 0xff);
            return;
        }
        if (address >= 0x6c000 && address <= 0x6cfff) {
            ram_clut_[(address & 0xfff) >> 1] = value;
            return;
        }
        if (address >= 0x70000 && address <= 0x70fff) {
            ram_fg_[(address & 0xfff) >> 1] = value;
            return;
        }
        if (address >= 0x74000 && address <= 0x74fff) {
            ram_bg_[(address & 0xfff) >> 1] = value;
            return;
        }
        // Terra Force control ($7c000..) — NB1414M4 stubbed
        switch (address) {
            case 0x7c000:
                if ((value & 0x4000) != 0 && (prev_video_reg_ & 0x4000) == 0) {
                    nb1414_.exec(scroll_fg_x_, scroll_fg_y_, frame_counter_);
                }
                video_reg_ = value;
                prev_video_reg_ = value;
                break;
            case 0x7c002: scroll_bg_x_ = value; break;
            case 0x7c004: scroll_bg_y_ = value; break;
            case 0x7c00a: sound_latch_ = uint8_t(((value & 0x7f) << 1) | 1); break;
            case 0x7c00e: main_cpu_.set_irq(irq_level_, IrqLine::Clear); break;
            default: break;
        }
        return;
    }

    // Armed F
    if (address >= 0x60000 && address <= 0x60fff) {
        ram_sprites_[(address & 0xfff) >> 1] = value;
        return;
    }
    if ((address >= 0x61000 && address <= 0x65fff) ||
        (address >= 0x6c000 && address <= 0x6c7ff)) {
        const uint32_t idx = (address - 0x60000) >> 1;
        if (idx < ram_.size()) ram_[idx] = value;
        return;
    }
    if (address >= 0x66000 && address <= 0x66fff) {
        ram_bg_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0x67000 && address <= 0x67fff) {
        ram_fg_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0x68000 && address <= 0x69fff) {
        ram_txt_[(address & 0x1fff) >> 1] = uint8_t(value & 0xff);
        return;
    }
    if (address >= 0x6a000 && address <= 0x6afff) {
        const int pos = int((address & 0xfff) >> 1);
        if (palette_ram_[size_t(pos)] != value) {
            palette_ram_[size_t(pos)] = value;
            set_color(pos, value);
        }
        return;
    }
    if (address >= 0x6b000 && address <= 0x6bfff) {
        ram_clut_[(address & 0xfff) >> 1] = value;
        return;
    }
    switch (address) {
        case 0x6d000: video_reg_ = value; break;
        case 0x6d002: scroll_bg_x_ = value; break;
        case 0x6d004: scroll_bg_y_ = value; break;
        case 0x6d006: scroll_fg_x_ = value; break;
        case 0x6d008: scroll_fg_y_ = value; break;
        case 0x6d00a: sound_latch_ = uint8_t(((value & 0x7f) << 1) | 1); break;
        case 0x6d00e: main_cpu_.set_irq(irq_level_, IrqLine::Clear); break;
        default: break;
    }
}

uint8_t ArmedF::main_read_byte(uint32_t address) {
    const uint16_t w = main_read_word(address & ~1u);
    return (address & 1) ? uint8_t(w & 0xff) : uint8_t(w >> 8);
}

void ArmedF::main_write_byte(uint32_t address, uint8_t value) {
    const uint32_t a = address & ~1u;
    uint16_t w = main_read_word(a);
    if (address & 1) w = uint16_t((w & 0xff00) | value);
    else w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
    main_write_word(a, w);
}

uint8_t ArmedF::sound_read(uint16_t address) { return mem_snd_[address]; }

void ArmedF::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0xf800) mem_snd_[address] = value;
}

uint8_t ArmedF::sound_in(uint16_t port) {
    switch (port & 0xff) {
        case 4: sound_latch_ = 0; return 0;
        case 6: return sound_latch_;
        default: return 0xff;
    }
}

void ArmedF::sound_out(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0: ym_.control(value); break;
        case 1: ym_.write(value); break;
        case 2:
            // signed_data8_w: treat as int8
            dac0_.data8_w(uint8_t(int8_t(value) + 0x80));
            break;
        case 3:
            dac1_.data8_w(uint8_t(int8_t(value) + 0x80));
            break;
        default: break;
    }
}

void ArmedF::on_sound_cycles(int cycles) {
    // IRQ every 512 Z80 clocks (timers.init 4000000/(4000000/512)).
    sound_irq_counter_ += cycles;
    while (sound_irq_counter_ >= 512) {
        sound_irq_counter_ -= 512;
        sound_cpu_.set_irq(IrqLine::Hold);
    }

    audio_accum_ += int64_t(cycles) * YM3812::kSampleRate;
    while (audio_accum_ >= kSoundClock) {
        audio_accum_ -= kSoundClock;
        int32_t sample = ym_.update() + dac0_.update() + dac1_.update();
        sample = std::clamp(sample, int32_t(-32768), int32_t(32767));
        audio_.push_back(int16_t(sample));
    }
}

void ArmedF::draw_text_layer(bool opaque) {
    auto& dest = opaque ? layer_txt_op_ : layer_txt_tr_;
    dest.assign(512 * 256, kTransparent);
    for (int f = 0; f < 0x800; f++) {
        const int x = f / 32;
        const int y = f % 32;
        int pos = f;
        if (game_ != Game::ArmedF) {
            // calc_pos_terraf
            pos = 32 * (31 - y) + (x & 0x1f) + 0x800 * (x / 32);
        }
        uint8_t atrib = 0;
        int nchar = 0;
        if (game_ != Game::ArmedF && pos < 0x12) {
            // protect parameter area
        } else if (game_ == Game::ArmedF) {
            atrib = ram_txt_[0x800 + pos];
            nchar = int(ram_txt_[pos]) + ((atrib & 3) << 8);
        } else {
            atrib = ram_txt_[0x400 + (pos & 0x3ff)];
            nchar = int(ram_txt_[pos & 0xfff]) + ((atrib & 3) << 8);
        }
        const int color = (atrib >> 4) & 0x0f;
        const int color_base = color << 4;
        const uint8_t* pixels = chars_.element(nchar);
        if (opaque) {
            blit_opaque(dest, 512, 256, x * 8, y * 8, pixels, 8, 8, palette_.data(), color_base,
                        false, false);
        } else {
            if ((atrib & 8) != 0) continue;
            blit_trans(dest, 512, 256, x * 8, y * 8, pixels, 8, 8, palette_.data(), color_base,
                       false, false, 15);
        }
    }
}

void ArmedF::draw_tile_layer(const std::array<uint16_t, 0x800>& ram, const GfxSet& gfx,
                             int color_base_shift, int /*scroll_x*/, int /*scroll_y*/) {
    // Rebuild full 64x32 tilemap into layer (scroll applied at composite time).
    // color_base_shift: 0x400 for FG, 0x600 for BG (see Pascal draw_fg_bg).
    std::vector<uint32_t>& dest = (color_base_shift == 0x600) ? layer_bg_ : layer_fg_;
    dest.assign(1024 * 512, kTransparent);
    for (int f = 0; f < 0x800; f++) {
        const int x = f / 32;
        const int y = f % 32;
        const uint16_t atrib = ram[size_t(f)];
        const int color = atrib >> 11;
        const int nchar = atrib & 0x3ff;
        const int color_base = (color << 4) + color_base_shift;
        const uint8_t* pixels = gfx.element(nchar);
        blit_trans(dest, 1024, 512, x * 16, y * 16, pixels, 16, 16, palette_.data(), color_base,
                   false, false, 15);
    }
}

void ArmedF::draw_sprites(int priority) {
    for (int f = 0; f <= sprite_num_; f++) {
        const uint16_t w0 = sprite_buffer_[size_t(f * 4 + 0)];
        const int pri = (w0 & 0x3000) >> 12;
        if (pri != priority) continue;
        const uint16_t nchar_raw = sprite_buffer_[size_t(f * 4 + 1)];
        const bool flip_x = (nchar_raw & 0x2000) != 0;
        const bool flip_y = (nchar_raw & 0x1000) != 0;
        const int nchar = nchar_raw & 0xfff;
        const uint16_t atrib = sprite_buffer_[size_t(f * 4 + 2)];
        const int color = (atrib >> 8) & 0x1f;
        const int clut = atrib & 0x7f;
        const int sx = sprite_buffer_[size_t(f * 4 + 3)] & 0x1ff;
        const int sy = int(sprite_offset_) + 240 - int(w0 & 0x1ff);

        const uint8_t* pixels = sprites_.element(nchar);
        // CLUT: each pixel indexes ram_clut[clut*16 + pix] & 0xf
        for (int y = 0; y < 16; y++) {
            const int dy = sy + y;
            if (dy < 0 || dy >= 512) continue;
            const int src_y = flip_y ? (15 - y) : y;
            for (int x = 0; x < 16; x++) {
                const int dx = sx + x;
                if (dx < 0 || dx >= 512) continue;
                const int src_x = flip_x ? (15 - x) : x;
                const uint8_t pix = pixels[src_y * 16 + src_x];
                const int punto = ram_clut_[size_t(clut * 16 + pix)] & 0xf;
                if (punto == 15) continue;
                composite_[size_t(dy * 512 + (dx & 0x1ff))] =
                    palette_[size_t(punto + (color << 4) + 0x200)];
            }
        }
    }
}

void ArmedF::update_video() {
    // Text + tilemaps
    draw_text_layer(true);
    draw_text_layer(false);
    draw_tile_layer(ram_bg_, tiles_bg_, 0x600, scroll_bg_x_, scroll_bg_y_);
    draw_tile_layer(ram_fg_, tiles_fg_, 0x400, scroll_fg_x_, scroll_fg_y_);

    // Composite (video_reg layer enables — Pascal update_video_armedf).
    std::fill(composite_.begin(), composite_.end(), palette_[0x800 & 0x7ff]);  // fallback black
    if ((video_reg_ & 0x100) != 0) {
        // Opaque text
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 512; x++)
                composite_[size_t(y * 512 + x)] = layer_txt_op_[size_t(y * 512 + x)];
    } else {
        std::fill(composite_.begin(), composite_.end(), 0xff000000u);
    }

    auto scroll_blit = [&](const std::vector<uint32_t>& src, int src_w, int src_h, int sx,
                           int sy) {
        for (int y = 0; y < 512; y++) {
            const int src_y = (y + sy) & (src_h - 1);
            for (int x = 0; x < 512; x++) {
                const int src_x = (x + sx) & (src_w - 1);
                const uint32_t p = src[size_t(src_y * src_w + src_x)];
                if (p == kTransparent || (p & 0x00ffffffu) == 0) continue;
                composite_[size_t(y * 512 + x)] = p;
            }
        }
    };

    if ((video_reg_ & 0x800) != 0)
        scroll_blit(layer_bg_, 1024, 512, scroll_bg_x_, scroll_bg_y_);
    if ((video_reg_ & 0x200) != 0) draw_sprites(2);
    if ((video_reg_ & 0x400) != 0)
        scroll_blit(layer_fg_, 1024, 512, scroll_fg_x_, scroll_fg_y_);
    if ((video_reg_ & 0x200) != 0) draw_sprites(1);
    if ((video_reg_ & 0x100) != 0) {
        // Transparent text overlay
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 512; x++) {
                const uint32_t p = layer_txt_tr_[size_t(y * 512 + x)];
                if (p == kTransparent) continue;
                composite_[size_t(y * 512 + x)] = p;
            }
        }
    }
    if ((video_reg_ & 0x200) != 0) draw_sprites(0);

    // Crop: actualiza_trozo_final(96, 8, 320, 240, 5) then optional rot270.
    constexpr int kCropX = 96;
    constexpr int kCropY = 8;
    constexpr int kCropW = kScreenWidth;   // 320
    constexpr int kCropH = kScreenHeight;  // 240

    if (rotate_270_) {
        // 270° clockwise: (x,y) -> (y, W-1-x), output 240 x 320
        for (int y = 0; y < kCropH; y++) {
            for (int x = 0; x < kCropW; x++) {
                const uint32_t pix = composite_[size_t((y + kCropY) * 512 + (x + kCropX))];
                const int dx = y;
                const int dy = kCropW - 1 - x;
                framebuffer_[size_t(dy * display_width_ + dx)] = pix;
            }
        }
    } else {
        for (int y = 0; y < kCropH; y++) {
            const uint32_t* src = &composite_[size_t((y + kCropY) * 512 + kCropX)];
            uint32_t* dst = &framebuffer_[size_t(y * kCropW)];
            std::copy(src, src + kCropW, dst);
        }
    }

    // Snapshot sprite RAM for next frame.
    sprite_buffer_ = ram_sprites_;
}

void ArmedF::run_frame() {
    const int cycles_main =
        int(double(kMainClock) / (kFramesPerSecond * kScanlines) + 0.5);
    const int cycles_sound =
        int(double(kSoundClock) / (kFramesPerSecond * kScanlines) + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 248) {
            main_cpu_.set_irq(irq_level_, IrqLine::Assert);
            update_video();
        }
        main_cpu_.run(cycles_main);
        sound_cpu_.run(cycles_sound);
    }
    ++frame_counter_;
}

void ArmedF::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
