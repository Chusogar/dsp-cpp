#include "drivers/gameboy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

// DMG boot ROM is optional. Without it we start at the cart entry point with
// registers already set the way the boot ROM would leave them.
constexpr uint8_t kPostBootA = 0x01;
constexpr uint8_t kPostBootF = 0xB0;
constexpr uint8_t kPostBootB = 0x00;
constexpr uint8_t kPostBootC = 0x13;
constexpr uint8_t kPostBootD = 0x00;
constexpr uint8_t kPostBootE = 0xD8;
constexpr uint8_t kPostBootH = 0x01;
constexpr uint8_t kPostBootL = 0x4D;
constexpr uint16_t kPostBootSP = 0xFFFE;
constexpr uint16_t kPostBootPC = 0x0100;

// TIMA clock divisors for TAC bits 1-0 (CPU cycles between ticks).
constexpr int kTimaPeriods[4] = {1024, 16, 64, 256};

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

}  // namespace

GameBoy::GameBoy() : cpu_(kCpuClock) {
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0);
    build_dmg_palette();
}

bool GameBoy::init(const std::string& rom_path, std::string* error) {
    if (!load_cartridge(rom_path, error)) return false;

    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    reset();
    return true;
}

void GameBoy::reset() {
    cpu_.reset();
    std::fill(vram_[0].begin(), vram_[0].end(), 0);
    std::fill(vram_[1].begin(), vram_[1].end(), 0);
    for (auto& bank : wram_) std::fill(bank.begin(), bank.end(), 0);
    std::fill(oam_.begin(), oam_.end(), 0);
    std::fill(hram_.begin(), hram_.end(), 0);
    std::fill(io_.begin(), io_.end(), 0);

    lcd_control_ = 0x91;
    stat_ = 0x85;
    scroll_y_ = scroll_x_ = ly_ = lyc_ = 0;
    window_y_ = window_x_ = 0;
    bgp_ = 0xFC;
    obp0_ = obp1_ = 0xFF;
    lcd_enabled_ = true;

    div_ = 0;
    tima_ = tma_ = tac_ = 0;
    div_counter_ = 0;
    tima_counter_ = 0;

    joy_select_ = 0xCF;
    joy_buttons_ = 0xFF;

    oam_dma_ = false;
    oam_dma_pos_ = 0;
    vram_bank_ = 0;
    wram_bank_ = 1;
    mapper_.reset();
    apu_.reset();
    line_cycles_ = 0;
    window_y_draw_ = 0;
    line_prio_.fill(0);
    bgcolor_index_ = spcolor_index_ = 0;
    bgcolor_inc_ = spcolor_inc_ = false;
    bgc_pal_.fill(0x7FFF);
    spc_pal_.fill(0x7FFF);
    hdma_src_ = hdma_dst_ = 0;
    hdma_size_ = 0xFF;
    hdma_active_ = false;

    audio_.clear();
    audio_accumulator_ = 0;
    std::fill(framebuffer_.begin(), framebuffer_.end(), dmg_palette_[0]);

    if (bios_enabled_ && bios_rom_[0] != 0) {
        cpu_.set_pc(0x0000);
    } else {
        // Skip boot: mimic post-boot register state.
        bios_enabled_ = false;
        cpu_.a = kPostBootA;
        cpu_.f = kPostBootF;
        cpu_.b = kPostBootB;
        cpu_.c = kPostBootC;
        cpu_.d = kPostBootD;
        cpu_.e = kPostBootE;
        cpu_.h = kPostBootH;
        cpu_.l = kPostBootL;
        cpu_.sp = kPostBootSP;
        cpu_.set_pc(kPostBootPC);
    }
}

// ---------------------------------------------------------------------------
// Cartridge
// ---------------------------------------------------------------------------

bool GameBoy::load_cartridge(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        if (error) {
            *error = "cannot open cartridge: " + path +
                     " (pass a .gb/.gbc file)";
        }
        return false;
    }
    const auto size = in.tellg();
    if (size < 0x150) {
        if (error) *error = "cartridge too small (missing header)";
        return false;
    }
    std::vector<uint8_t> image(size_t(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(image.data()), size);
    if (!in) {
        if (error) *error = "failed to read cartridge data";
        return false;
    }

    if (!mapper_.load(image, /*crc32=*/0)) {
        if (error) *error = "unsupported or corrupt cartridge header";
        return false;
    }
    apply_cart_header();

    // Battery save next to the ROM: path.sav
    if (mapper_.has_battery()) {
        save_ram_path_ = path + ".sav";
        mapper_.load_ram(save_ram_path_);  // ignore failure (first run)
    } else {
        save_ram_path_.clear();
    }
    return true;
}

void GameBoy::apply_cart_header() {
    is_gbc_ = mapper_.is_gbc();
}

bool GameBoy::load_boot_rom(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open boot ROM: " + path;
        return false;
    }
    in.read(reinterpret_cast<char*>(bios_rom_.data()), 0x100);
    if (!in && !in.eof()) {
        if (error) *error = "failed to read boot ROM";
        return false;
    }
    bios_enabled_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Memory map  (gb_getbyte / gb_putbyte)
// ---------------------------------------------------------------------------

uint8_t GameBoy::read_byte(uint16_t address) {
    if (address < 0x100) {
        if (bios_enabled_) return bios_rom_[address];
    }

    if (address < 0x8000) {
        return mapper_.read_rom(address);
    }
    if (address < 0xA000) {
        return vram_[vram_bank_ & 1][address & 0x1FFF];
    }
    if (address < 0xC000) {
        return mapper_.read_ram(address);
    }
    if (address < 0xD000) {
        return wram_[0][address & 0x0FFF];
    }
    if (address < 0xE000) {
        return wram_[wram_bank_ & 7][address & 0x0FFF];
    }
    if (address < 0xFE00) {
        // Echo RAM
        return read_byte(uint16_t(address - 0x2000));
    }
    if (address < 0xFEA0) {
        return oam_[address & 0xFF];
    }
    if (address < 0xFF00) {
        return 0x00;  // unusable
    }
    if (address < 0xFF80) {
        return read_io(uint8_t(address & 0xFF));
    }
    // HRAM + IE at $FFFF
    return hram_[address & 0xFF];
}

void GameBoy::write_byte(uint16_t address, uint8_t value) {
    if (address < 0x8000) {
        mapper_.write_rom(address, value);
        return;
    }
    if (address < 0xA000) {
        vram_[vram_bank_ & 1][address & 0x1FFF] = value;
        return;
    }
    if (address < 0xC000) {
        mapper_.write_ram(address, value);
        return;
    }
    if (address < 0xD000) {
        wram_[0][address & 0x0FFF] = value;
        return;
    }
    if (address < 0xE000) {
        wram_[wram_bank_ & 7][address & 0x0FFF] = value;
        return;
    }
    if (address < 0xFE00) {
        write_byte(uint16_t(address - 0x2000), value);
        return;
    }
    if (address < 0xFEA0) {
        oam_[address & 0xFF] = value;
        return;
    }
    if (address < 0xFF00) {
        return;  // unusable
    }
    if (address < 0xFF80) {
        write_io(uint8_t(address & 0xFF), value);
        return;
    }
    hram_[address & 0xFF] = value;
    if (address == 0xFFFF) {
        // IE
        cpu_.set_vblank_enable((value & 0x01) != 0);
        cpu_.set_lcdstat_enable((value & 0x02) != 0);
        cpu_.set_timer_enable((value & 0x04) != 0);
        cpu_.set_serial_enable((value & 0x08) != 0);
        cpu_.set_joystick_enable((value & 0x10) != 0);
    }
}

// ---------------------------------------------------------------------------
// I/O ports  (leer_io / escribe_io, DMG subset)
// ---------------------------------------------------------------------------

uint8_t GameBoy::read_io(uint8_t port) {
    switch (port) {
        case 0x00: {  // P1
            uint8_t result = joy_select_ | 0x0F;
            if ((joy_select_ & 0x10) == 0) {
                // Directions on bits 0-3 of joy_buttons_
                result = (result & 0xF0) | (joy_buttons_ & 0x0F);
            }
            if ((joy_select_ & 0x20) == 0) {
                // Buttons on bits 4-7
                result = (result & 0xF0) | ((joy_buttons_ >> 4) & 0x0F);
            }
            return result | 0xC0;
        }
        case 0x04:
            return div_;
        case 0x05:
            return tima_;
        case 0x06:
            return tma_;
        case 0x07:
            return uint8_t(0xF8 | (tac_ & 0x07));
        case 0x0F: {  // IF
            uint8_t v = 0xE0;
            if (cpu_.vblank_request()) v |= 0x01;
            if (cpu_.lcdstat_request()) v |= 0x02;
            if (cpu_.timer_request()) v |= 0x04;
            if (cpu_.serial_request()) v |= 0x08;
            if (cpu_.joystick_request()) v |= 0x10;
            return v;
        }
        case 0x40:
            return lcd_control_;
        case 0x41:
            return uint8_t(0x80 | (stat_ & 0x7F));
        case 0x42:
            return scroll_y_;
        case 0x43:
            return scroll_x_;
        case 0x44:
            return ly_;
        case 0x45:
            return lyc_;
        case 0x47:
            return bgp_;
        case 0x48:
            return obp0_;
        case 0x49:
            return obp1_;
        case 0x4A:
            return window_y_;
        case 0x4B:
            return window_x_;
        case 0x4D:  // KEY1
            if (!is_gbc_) return 0xFF;
            return uint8_t((cpu_.speed() << 7) | 0x7E |
                           (/* change pending mirrored via io_ */ io_[0x4D] & 1));
        case 0x4F:  // VBK
            return is_gbc_ ? uint8_t(0xFE | (vram_bank_ & 1)) : 0xFF;
        case 0x55:  // HDMA5
            return is_gbc_ ? hdma_size_ : 0xFF;
        case 0x68:  // BCPS
            return is_gbc_ ? uint8_t((bgcolor_inc_ ? 0x80 : 0) | (bgcolor_index_ & 0x3F))
                           : 0xFF;
        case 0x69: {  // BCPD
            if (!is_gbc_) return 0xFF;
            const uint16_t c = bgc_pal_[(bgcolor_index_ >> 1) & 0x1F];
            return (bgcolor_index_ & 1) ? uint8_t(c >> 8) : uint8_t(c & 0xFF);
        }
        case 0x6A:  // OCPS
            return is_gbc_ ? uint8_t((spcolor_inc_ ? 0x80 : 0) | (spcolor_index_ & 0x3F))
                           : 0xFF;
        case 0x6B: {  // OCPD
            if (!is_gbc_) return 0xFF;
            const uint16_t c = spc_pal_[(spcolor_index_ >> 1) & 0x1F];
            return (spcolor_index_ & 1) ? uint8_t(c >> 8) : uint8_t(c & 0xFF);
        }
        case 0x70:  // SVBK
            return is_gbc_ ? uint8_t(0xF8 | (wram_bank_ & 7)) : 0xFF;
        default:
            if (port >= 0x10 && port <= 0x26) {
                return apu_.sound_r(uint8_t(port - 0x10));
            }
            if (port >= 0x30 && port <= 0x3F) {
                return apu_.wave_r(uint8_t(port - 0x30));
            }
            return io_[port & 0x7F];
    }
}

void GameBoy::write_io(uint8_t port, uint8_t value) {
    io_[port & 0x7F] = value;
    switch (port) {
        case 0x00:  // P1
            joy_select_ = uint8_t(0xCF | (value & 0x30));
            break;
        case 0x04:  // DIV: any write clears it
            div_ = 0;
            div_counter_ = 0;
            break;
        case 0x05:
            tima_ = value;
            break;
        case 0x06:
            tma_ = value;
            break;
        case 0x07:
            tac_ = value & 0x07;
            break;
        case 0x0F:  // IF
            if (value & 0x01)
                cpu_.request_vblank();
            else
                cpu_.clear_vblank();
            if (value & 0x02)
                cpu_.request_lcdstat();
            else
                cpu_.clear_lcdstat();
            if (value & 0x04)
                cpu_.request_timer();
            else
                cpu_.clear_timer();
            if (value & 0x08)
                cpu_.request_serial();
            else
                cpu_.clear_serial();
            if (value & 0x10)
                cpu_.request_joystick();
            else
                cpu_.clear_joystick();
            break;
        case 0x40:  // LCDC
            lcd_control_ = value;
            lcd_enabled_ = (value & 0x80) != 0;
            if (!lcd_enabled_) {
                ly_ = 0;
                line_cycles_ = 0;
                stat_ = uint8_t((stat_ & 0xFC) | 0x00);
            }
            break;
        case 0x41:  // STAT (mode bits read-only)
            stat_ = uint8_t((stat_ & 0x07) | (value & 0x78));
            break;
        case 0x42:
            scroll_y_ = value;
            break;
        case 0x43:
            scroll_x_ = value;
            break;
        case 0x45:
            lyc_ = value;
            break;
        case 0x46:  // OAM DMA
            do_oam_dma(value);
            break;
        case 0x47:
            bgp_ = value;
            break;
        case 0x48:
            obp0_ = value;
            break;
        case 0x49:
            obp1_ = value;
            break;
        case 0x4A:
            window_y_ = value;
            break;
        case 0x4B:
            window_x_ = value;
            break;
        case 0x50:  // boot ROM disable
            bios_enabled_ = false;
            break;
        case 0x4D:  // KEY1 prepare speed switch
            if (is_gbc_) {
                io_[0x4D] = value & 1;
                if (value & 1) cpu_.arm_speed_switch();
            }
            break;
        case 0x4F:  // VBK
            if (is_gbc_) vram_bank_ = value & 1;
            break;
        case 0x51:  // HDMA1 src hi
            if (is_gbc_) hdma_src_ = uint16_t((hdma_src_ & 0x00FF) | (value << 8));
            break;
        case 0x52:  // HDMA2 src lo (low 4 bits ignored)
            if (is_gbc_) hdma_src_ = uint16_t((hdma_src_ & 0xFF00) | (value & 0xF0));
            break;
        case 0x53:  // HDMA3 dst hi (only low 5 bits used -> $8000-$9FFF)
            if (is_gbc_) hdma_dst_ = uint16_t((hdma_dst_ & 0x00FF) | ((value & 0x1F) << 8));
            break;
        case 0x54:  // HDMA4 dst lo
            if (is_gbc_) hdma_dst_ = uint16_t((hdma_dst_ & 0xFF00) | (value & 0xF0));
            break;
        case 0x55:  // HDMA5
            if (!is_gbc_) break;
            if (hdma_active_ && (value & 0x80)) {
                // Cancel
                hdma_active_ = false;
                hdma_size_ = uint8_t(hdma_size_ | 0x80);
            } else if (value & 0x80) {
                // Start HBlank DMA
                hdma_size_ = value & 0x7F;
                hdma_active_ = true;
            } else {
                // General-purpose DMA: transfer (value+1)*16 bytes immediately
                const int bytes = (int(value) + 1) * 0x10;
                do_hdma_block(bytes);
                hdma_size_ = 0xFF;
                hdma_active_ = false;
            }
            break;
        case 0x68:  // BCPS
            if (is_gbc_) {
                bgcolor_inc_ = (value & 0x80) != 0;
                bgcolor_index_ = value & 0x3F;
            }
            break;
        case 0x69: {  // BCPD
            if (!is_gbc_) break;
            uint16_t& c = bgc_pal_[(bgcolor_index_ >> 1) & 0x1F];
            if (bgcolor_index_ & 1)
                c = uint16_t((c & 0x00FF) | (value << 8));
            else
                c = uint16_t((c & 0xFF00) | value);
            c &= 0x7FFF;
            if (bgcolor_inc_) bgcolor_index_ = uint8_t((bgcolor_index_ + 1) & 0x3F);
            break;
        }
        case 0x6A:  // OCPS
            if (is_gbc_) {
                spcolor_inc_ = (value & 0x80) != 0;
                spcolor_index_ = value & 0x3F;
            }
            break;
        case 0x6B: {  // OCPD
            if (!is_gbc_) break;
            uint16_t& c = spc_pal_[(spcolor_index_ >> 1) & 0x1F];
            if (spcolor_index_ & 1)
                c = uint16_t((c & 0x00FF) | (value << 8));
            else
                c = uint16_t((c & 0xFF00) | value);
            c &= 0x7FFF;
            if (spcolor_inc_) spcolor_index_ = uint8_t((spcolor_index_ + 1) & 0x3F);
            break;
        }
        case 0x70:  // SVBK
            if (is_gbc_) {
                wram_bank_ = value & 0x07;
                if (wram_bank_ == 0) wram_bank_ = 1;
            }
            break;
        default:
            if (port >= 0x10 && port <= 0x26) {
                apu_.sound_w(uint8_t(port - 0x10), value);
            } else if (port >= 0x30 && port <= 0x3F) {
                apu_.wave_w(uint8_t(port - 0x30), value);
            }
            break;
    }
}

void GameBoy::do_oam_dma(uint8_t page) {
    // Copy 160 bytes from page*0x100 to OAM. Takes 160 machine cycles;
    // we do it instantly for the skeleton and flag the busy window.
    const uint16_t src = uint16_t(page) << 8;
    for (int i = 0; i < 0xA0; i++) {
        oam_[i] = read_byte(uint16_t(src + i));
    }
    oam_dma_ = true;
    oam_dma_pos_ = 0;
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

void GameBoy::update_timer(int cycles) {
    // DIV: increments every 256 T-states (CPU cycles).
    div_counter_ += cycles;
    while (div_counter_ >= 256) {
        div_counter_ -= 256;
        div_ = uint8_t(div_ + 1);
    }

    if ((tac_ & 0x04) == 0) return;  // timer stopped
    const int period = kTimaPeriods[tac_ & 0x03];
    tima_counter_ += cycles;
    while (tima_counter_ >= period) {
        tima_counter_ -= period;
        if (tima_ == 0xFF) {
            tima_ = tma_;
            cpu_.request_timer();
        } else {
            tima_ = uint8_t(tima_ + 1);
        }
    }
}

void GameBoy::update_lcd_stat(int /*cycles_in_line*/) {
    if (!lcd_enabled_) return;

    const bool lyc_match = (ly_ == lyc_);
    if (lyc_match)
        stat_ |= 0x04;
    else
        stat_ &= uint8_t(~0x04);

    // Mode selection based on position in the line (simplified).
    uint8_t mode = 0;
    if (ly_ >= 144) {
        mode = 1;  // VBlank
    } else if (line_cycles_ < 80) {
        mode = 2;  // OAM search
    } else if (line_cycles_ < 252) {
        mode = 3;  // pixel transfer
    } else {
        mode = 0;  // HBlank
    }
    const uint8_t old_mode = stat_ & 0x03;
    stat_ = uint8_t((stat_ & 0xFC) | mode);

    bool fire = false;
    if (lyc_match && (stat_ & 0x40)) fire = true;
    if (mode != old_mode) {
        if (mode == 0 && (stat_ & 0x08)) fire = true;
        if (mode == 1 && (stat_ & 0x10)) fire = true;
        if (mode == 2 && (stat_ & 0x20)) fire = true;
    }
    if (fire) cpu_.request_lcdstat();
}

void GameBoy::on_cycles(int cycles) {
    update_timer(cycles);

    if (oam_dma_) {
        oam_dma_pos_ += cycles;
        if (oam_dma_pos_ >= 160 * 4) oam_dma_ = false;  // ~160 M-cycles
    }

    // Convert CPU cycles into APU samples @ 44100 Hz.
    audio_accumulator_ += int64_t(cycles) * GBSound::kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        audio_.push_back(apu_.update());
    }

    if (cpu_.changed_speed()) {
        // GBC double-speed would re-scale timers here.
        cpu_.clear_changed_speed();
    }
}

// ---------------------------------------------------------------------------
// Video — DMG renderer (update_video_gb priority model)
// ---------------------------------------------------------------------------
//
// Pass order (from the Pascal comments):
//   1. BG pass 0          — every shade opaque
//   2. Window pass 0      — every shade opaque, marks prio bit1
//   3. Sprites pri=1      — OBJ behind BG (attr bit7 set), colour 0 transparent
//   4. BG pass 1          — colour 0 transparent; non-zero covers sprites unless
//                           a window pixel sits under that column
//   5. Window pass 1      — colour 0 transparent only over a sprite
//   6. Sprites pri=0      — OBJ in front, colour 0 transparent
//
// Output is written straight into framebuffer_ (ARGB8888). BGP/OBP map the
// 2-bit colour id to one of the four DMG shades.

void GameBoy::build_dmg_palette() {
    if (palette_kind_ == Palette::Green) {
        // color_pal[0] from gb.pas
        dmg_palette_[0] = rgb(0x9B, 0xBC, 0x0F);
        dmg_palette_[1] = rgb(0x8B, 0xAC, 0x0F);
        dmg_palette_[2] = rgb(0x30, 0x62, 0x30);
        dmg_palette_[3] = rgb(0x0F, 0x38, 0x0F);
    } else {
        dmg_palette_[0] = rgb(0xFF, 0xFF, 0xFF);
        dmg_palette_[1] = rgb(0xAA, 0xAA, 0xAA);
        dmg_palette_[2] = rgb(0x55, 0x55, 0x55);
        dmg_palette_[3] = rgb(0x00, 0x00, 0x00);
    }
    for (int i = 4; i < 12; i++) dmg_palette_[i] = dmg_palette_[i & 3];
}

void GameBoy::set_palette(Palette palette) {
    palette_kind_ = palette;
    build_dmg_palette();
}

uint8_t GameBoy::bgp_shade(uint8_t color_id) const {
    return uint8_t((bgp_ >> (color_id * 2)) & 0x03);
}

uint8_t GameBoy::obp_shade(uint8_t palette_reg, uint8_t color_id) const {
    return uint8_t((palette_reg >> (color_id * 2)) & 0x03);
}

void GameBoy::fetch_tile_line(int tile_id, bool signed_addressing, int row_in_tile,
                             uint8_t bank, uint8_t* out_lo, uint8_t* out_hi) const {
    uint16_t addr;
    if (signed_addressing) {
        addr = uint16_t(0x1000 + int16_t(int8_t(tile_id)) * 16 + row_in_tile * 2);
    } else {
        addr = uint16_t(tile_id * 16 + row_in_tile * 2);
    }
    *out_lo = read_vram(bank, addr);
    *out_hi = read_vram(bank, uint16_t(addr + 1));
}

void GameBoy::get_active_sprites(uint8_t* ordered, int* count, bool gbc_order) const {
    // Up to 10 sprites. DMG sorts by X; GBC keeps OAM order.
    uint8_t xs[10];
    for (int i = 0; i < 10; i++) {
        ordered[i] = 0xFF;
        xs[i] = 0xFF;
    }
    *count = 0;
    const int sprite_h = (lcd_control_ & 0x04) ? 16 : 8;

    for (int s = 0; s < 40; s++) {
        const int pos_y = oam_[s * 4 + 0];
        if (pos_y == 0 || pos_y >= 160) continue;
        const int line_in_sprite = int(ly_) - (pos_y - 16);
        if (line_in_sprite < 0 || line_in_sprite >= sprite_h) continue;

        if (gbc_order) {
            ordered[*count] = uint8_t(s);
            (*count)++;
            if (*count >= 10) break;
            continue;
        }

        const uint8_t pos_x = oam_[s * 4 + 1];
        for (int h = 0; h < 10; h++) {
            if (xs[h] > pos_x) {
                for (int g = 8; g >= h; g--) {
                    xs[g + 1] = xs[g];
                    ordered[g + 1] = ordered[g];
                }
                xs[h] = pos_x;
                ordered[h] = uint8_t(s);
                if (*count < 10) (*count)++;
                break;
            }
        }
    }
}

void GameBoy::plot_bg_line_dmg(uint8_t* color_line, uint8_t* prio_line, int pass) {
    // Map base: LCDC bit3 selects $9800 or $9C00.
    const uint16_t map_base = uint16_t(0x1800 + ((lcd_control_ & 0x08) << 7));
    const bool signed_tiles = (lcd_control_ & 0x10) == 0;
    const int y = (ly_ + scroll_y_) & 0xFF;
    const int tile_row = y >> 3;
    const int row_in_tile = y & 7;

    for (int screen_x = 0; screen_x < kScreenWidth; screen_x++) {
        const int x = (screen_x + scroll_x_) & 0xFF;
        const int tile_col = x >> 3;
        const int bit = 7 - (x & 7);

        const uint16_t map_addr = uint16_t(map_base + tile_row * 32 + tile_col);
        const uint8_t tile_id = read_vram(0, map_addr);

        uint8_t lo, hi;
        fetch_tile_line(tile_id, signed_tiles, row_in_tile, 0, &lo, &hi);
        const uint8_t color_id =
            uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));

        if (pass == 0) {
            color_line[screen_x] = color_id;
        } else {
            // Pass 1: colour 0 is transparent; non-zero covers sprites unless
            // a window pixel already claimed the column (prio bit1).
            if (color_id != 0 && (prio_line[screen_x] & 0x02) == 0) {
                color_line[screen_x] = color_id;
                // Clear sprite mark so later window pass sees pure BG.
            } else if (color_id == 0) {
                // leave whatever is there (sprite or previous BG)
            }
        }
    }
}

void GameBoy::plot_window_line_dmg(uint8_t* color_line, uint8_t* prio_line, int pass) {
    // Window is off, or not yet reached, or WX out of range.
    if ((lcd_control_ & 0x20) == 0) return;
    if (ly_ < window_y_) return;
    if (window_x_ == 0 || window_x_ > 166) return;

    const uint16_t map_base = uint16_t(0x1800 + ((lcd_control_ & 0x40) << 4));
    const bool signed_tiles = (lcd_control_ & 0x10) == 0;
    const int y = window_y_draw_;
    const int tile_row = y >> 3;
    const int row_in_tile = y & 7;
    // WX is offset by 7: screen X where window starts = WX - 7.
    const int win_origin = int(window_x_) - 7;

    for (int screen_x = 0; screen_x < kScreenWidth; screen_x++) {
        if (screen_x < win_origin) continue;
        const int x = screen_x - win_origin;
        const int tile_col = x >> 3;
        const int bit = 7 - (x & 7);

        const uint16_t map_addr =
            uint16_t(map_base + ((tile_row * 32 + tile_col) & 0x3FF));
        const uint8_t tile_id = read_vram(0, map_addr);

        uint8_t lo, hi;
        fetch_tile_line(tile_id, signed_tiles, row_in_tile, 0, &lo, &hi);
        const uint8_t color_id =
            uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));

        if (pass == 0) {
            color_line[screen_x] = color_id;
            prio_line[screen_x] |= 0x02;  // mark window
        } else {
            // Pass 1: colour 0 is transparent only over a sprite; otherwise
            // window is always opaque (matches Pascal update_window prio=1).
            if (color_id == 0) {
                if ((prio_line[screen_x] & 0x01) == 0) {
                    color_line[screen_x] = 0;
                }
                // else keep the sprite pixel underneath
            } else {
                color_line[screen_x] = color_id;
            }
        }
    }
}

void GameBoy::plot_sprites_line_dmg(uint8_t* color_line, uint8_t* prio_line,
                                const uint8_t* ordered, int count,
                                uint8_t pri_mask) {
    // pri_mask: $80 = behind BG, $00 = in front. Drawn from lowest priority
    // (rightmost in the X-sorted list) to highest so earlier X wins ties.
    const int sprite_h = (lcd_control_ & 0x04) ? 16 : 8;

    for (int i = count - 1; i >= 0; i--) {
        const uint8_t s = ordered[i];
        if (s == 0xFF) continue;
        const uint8_t attr = oam_[s * 4 + 3];
        if ((attr & 0x80) != pri_mask) continue;

        const int pos_y = int(oam_[s * 4 + 0]) - 16;
        const int pos_x = int(oam_[s * 4 + 1]) - 8;
        if (pos_x <= -8 || pos_x >= 168) continue;

        int line_in = int(ly_) - pos_y;
        uint8_t tile = oam_[s * 4 + 2];
        const bool flip_x = (attr & 0x20) != 0;
        const bool flip_y = (attr & 0x40) != 0;

        if (sprite_h == 16) {
            tile &= 0xFE;
            if (flip_y) {
                line_in = 15 - line_in;
            }
            if (line_in >= 8) {
                tile |= 1;
                line_in -= 8;
            }
        } else if (flip_y) {
            line_in = 7 - line_in;
        }

        // Sprites always use unsigned $8000 addressing.
        uint8_t lo = read_vram(0, uint16_t(tile * 16 + line_in * 2));
        uint8_t hi = read_vram(0, uint16_t(tile * 16 + line_in * 2 + 1));
        const uint8_t palette_reg = (attr & 0x10) ? obp1_ : obp0_;

        for (int px = 0; px < 8; px++) {
            const int bit = flip_x ? px : (7 - px);
            const uint8_t color_id =
                uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            if (color_id == 0) continue;  // transparent

            const int screen_x = pos_x + px;
            if (screen_x < 0 || screen_x >= kScreenWidth) continue;

            if (pri_mask == 0x80) {
                // Behind BG: only draw where no BG/window non-zero will cover
                // later; we still paint now and mark the sprite bit so the BG
                // pass 1 can decide.
                color_line[screen_x] = uint8_t(0x80 | color_id);  // high bit = OBJ
                // Encode which OBP: bit6 selects OBP1.
                if (attr & 0x10) color_line[screen_x] |= 0x40;
                prio_line[screen_x] |= 0x01;
            } else {
                // In front: always cover, except we still skip colour 0.
                color_line[screen_x] = uint8_t(0x80 | color_id);
                if (attr & 0x10) color_line[screen_x] |= 0x40;
                prio_line[screen_x] |= 0x01;
            }
        }
    }
}

void GameBoy::render_scanline() {
    if (ly_ >= kScreenHeight) return;
    uint32_t* row = framebuffer_.data() + size_t(ly_) * kScreenWidth;
    if (!lcd_enabled_) {
        const uint32_t blank = is_gbc_ ? 0xFFFFFFFF : dmg_palette_[0];
        for (int x = 0; x < kScreenWidth; x++) row[x] = blank;
        return;
    }
    if (is_gbc_)
        render_scanline_gbc();
    else
        render_scanline_dmg();
}

void GameBoy::render_scanline_dmg() {
    uint32_t* row = framebuffer_.data() + size_t(ly_) * kScreenWidth;
    uint8_t color_line[160];
    uint8_t prio_line[160];
    std::fill(color_line, color_line + 160, 0);
    std::fill(prio_line, prio_line + 160, 0);
    line_prio_.fill(0);

    const bool bg_on = (lcd_control_ & 0x01) != 0;
    const bool obj_on = (lcd_control_ & 0x02) != 0 && !oam_dma_;

    uint8_t sprites[10];
    int sprite_count = 0;
    if (obj_on) get_active_sprites(sprites, &sprite_count, false);

    if (bg_on) {
        plot_bg_line_dmg(color_line, prio_line, 0);
        plot_window_line_dmg(color_line, prio_line, 0);
    }
    if (obj_on) plot_sprites_line_dmg(color_line, prio_line, sprites, sprite_count, 0x80);
    if (bg_on) {
        plot_bg_line_dmg(color_line, prio_line, 1);
        plot_window_line_dmg(color_line, prio_line, 1);
    }
    if (obj_on) plot_sprites_line_dmg(color_line, prio_line, sprites, sprite_count, 0x00);

    for (int x = 0; x < kScreenWidth; x++) {
        const uint8_t cell = color_line[x];
        uint8_t shade;
        if (cell & 0x80) {
            const uint8_t color_id = cell & 0x03;
            const uint8_t pre = (cell & 0x40) ? obp1_ : obp0_;
            shade = obp_shade(pre, color_id);
        } else {
            shade = bgp_shade(cell & 0x03);
        }
        row[x] = dmg_palette_[shade];
    }

    if ((lcd_control_ & 0x20) != 0 && ly_ >= window_y_ && window_x_ != 0 &&
        window_x_ <= 166) {
        window_y_draw_ = uint8_t(window_y_draw_ + 1);
    }
    std::copy(prio_line, prio_line + 160, line_prio_.begin());
}

uint32_t GameBoy::rgb555_to_argb(uint16_t c) {
    // GBC RGB555 -> 8-bit with simple bit replication.
    const uint8_t r5 = c & 0x1F;
    const uint8_t g5 = (c >> 5) & 0x1F;
    const uint8_t b5 = (c >> 10) & 0x1F;
    const uint8_t r = uint8_t((r5 << 3) | (r5 >> 2));
    const uint8_t g = uint8_t((g5 << 3) | (g5 >> 2));
    const uint8_t b = uint8_t((b5 << 3) | (b5 >> 2));
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void GameBoy::plot_bg_line_gbc(uint32_t* row, uint8_t* prio_line, int pass) {
    // On GBC, LCDC bit0 does not disable BG; it controls overall OBJ priority.
    const uint16_t map_base = uint16_t(0x1800 + ((lcd_control_ & 0x08) << 7));
    const bool signed_tiles = (lcd_control_ & 0x10) == 0;
    const int y = (ly_ + scroll_y_) & 0xFF;
    const int tile_row = y >> 3;
    const int row_in_tile_base = y & 7;

    for (int screen_x = 0; screen_x < kScreenWidth; screen_x++) {
        const int x = (screen_x + scroll_x_) & 0xFF;
        const int tile_col = x >> 3;
        const uint16_t map_addr = uint16_t(map_base + ((tile_row * 32 + tile_col) & 0x3FF));
        const uint8_t tile_id = read_vram(0, map_addr);
        const uint8_t attr = read_vram(1, map_addr);
        const uint8_t tile_bank = (attr >> 3) & 1;
        const uint8_t tile_pal = (attr & 7) << 2;
        const bool flip_x = (attr & 0x20) != 0;
        const bool flip_y = (attr & 0x40) != 0;
        const bool bg_prio = (attr & 0x80) != 0;

        int row_in_tile = flip_y ? (7 - row_in_tile_base) : row_in_tile_base;
        uint8_t lo, hi;
        fetch_tile_line(tile_id, signed_tiles, row_in_tile, tile_bank, &lo, &hi);
        const int bit = flip_x ? (x & 7) : (7 - (x & 7));
        const uint8_t color_id = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));

        if (pass == 0) {
            row[screen_x] = rgb555_to_argb(bgc_pal_[(color_id + tile_pal) & 0x1F]);
            if (color_id != 0 && bg_prio) prio_line[screen_x] |= 0x04;  // BG master priority
            // store colour id in low bits of prio for pass1 decisions: use bit7 = non-zero
            if (color_id != 0) prio_line[screen_x] |= 0x08;
        } else {
            // Pass 1: non-zero BG covers sprites unless window claimed the column.
            if (color_id != 0 && (prio_line[screen_x] & 0x02) == 0) {
                // Only if no sprite-behind already handled via priority...
                if ((prio_line[screen_x] & 0x01) == 0 || !(prio_line[screen_x] & 0x04)) {
                    // Always cover sprites with non-zero BG unless BG-priority bit
                    // is set and we want sprites in front - actually non-zero BG
                    // covers sprites that don't have front priority.
                }
                row[screen_x] = rgb555_to_argb(bgc_pal_[(color_id + tile_pal) & 0x1F]);
                prio_line[screen_x] |= 0x08;
                if (bg_prio) prio_line[screen_x] |= 0x04;
            }
        }
    }
}

void GameBoy::plot_window_line_gbc(uint32_t* row, uint8_t* prio_line, int pass) {
    if ((lcd_control_ & 0x20) == 0) return;
    if (ly_ < window_y_) return;
    if (window_x_ == 0 || window_x_ > 166) return;

    const uint16_t map_base = uint16_t(0x1800 + ((lcd_control_ & 0x40) << 4));
    const bool signed_tiles = (lcd_control_ & 0x10) == 0;
    const int y = window_y_draw_;
    const int tile_row = y >> 3;
    const int row_in_tile_base = y & 7;
    const int win_origin = int(window_x_) - 7;

    for (int screen_x = 0; screen_x < kScreenWidth; screen_x++) {
        if (screen_x < win_origin) continue;
        const int x = screen_x - win_origin;
        const int tile_col = x >> 3;
        const uint16_t map_addr = uint16_t(map_base + ((tile_row * 32 + tile_col) & 0x3FF));
        const uint8_t tile_id = read_vram(0, map_addr);
        const uint8_t attr = read_vram(1, map_addr);
        const uint8_t tile_bank = (attr >> 3) & 1;
        const uint8_t tile_pal = (attr & 7) << 2;
        const bool flip_x = (attr & 0x20) != 0;
        const bool flip_y = (attr & 0x40) != 0;
        const bool bg_prio = (attr & 0x80) != 0;

        int row_in_tile = flip_y ? (7 - row_in_tile_base) : row_in_tile_base;
        uint8_t lo, hi;
        fetch_tile_line(tile_id, signed_tiles, row_in_tile, tile_bank, &lo, &hi);
        const int bit = flip_x ? (x & 7) : (7 - (x & 7));
        const uint8_t color_id = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));

        if (pass == 0) {
            row[screen_x] = rgb555_to_argb(bgc_pal_[(color_id + tile_pal) & 0x1F]);
            prio_line[screen_x] |= 0x02;
            if (color_id != 0) prio_line[screen_x] |= 0x08;
            if (bg_prio && color_id != 0) prio_line[screen_x] |= 0x04;
        } else {
            if (color_id == 0) {
                if ((prio_line[screen_x] & 0x01) == 0) {
                    row[screen_x] = rgb555_to_argb(bgc_pal_[tile_pal & 0x1F]);
                }
            } else {
                row[screen_x] = rgb555_to_argb(bgc_pal_[(color_id + tile_pal) & 0x1F]);
                prio_line[screen_x] |= 0x08;
                if (bg_prio) prio_line[screen_x] |= 0x04;
            }
        }
    }
}

void GameBoy::plot_sprites_line_gbc(uint32_t* row, uint8_t* prio_line,
                                    const uint8_t* ordered, int count,
                                    uint8_t pri_mask) {
    const int sprite_h = (lcd_control_ & 0x04) ? 16 : 8;
    // LCDC bit0 on GBC: when clear, OBJ always has priority over BG/Window.
    const bool obj_master = (lcd_control_ & 0x01) == 0;

    for (int i = count - 1; i >= 0; i--) {
        const uint8_t s = ordered[i];
        if (s == 0xFF) continue;
        const uint8_t attr = oam_[s * 4 + 3];
        if ((attr & 0x80) != pri_mask) continue;

        const int pos_y = int(oam_[s * 4 + 0]) - 16;
        const int pos_x = int(oam_[s * 4 + 1]) - 8;
        if (pos_x <= -8 || pos_x >= 168) continue;

        int line_in = int(ly_) - pos_y;
        uint8_t tile = oam_[s * 4 + 2];
        const bool flip_x = (attr & 0x20) != 0;
        const bool flip_y = (attr & 0x40) != 0;
        const uint8_t spr_bank = (attr >> 3) & 1;
        const uint8_t pal = (attr & 7) * 4;

        if (sprite_h == 16) {
            tile &= 0xFE;
            if (flip_y) line_in = 15 - line_in;
            if (line_in >= 8) {
                tile |= 1;
                line_in -= 8;
            }
        } else if (flip_y) {
            line_in = 7 - line_in;
        }

        uint8_t lo = read_vram(spr_bank, uint16_t(tile * 16 + line_in * 2));
        uint8_t hi = read_vram(spr_bank, uint16_t(tile * 16 + line_in * 2 + 1));

        for (int px = 0; px < 8; px++) {
            const int bit = flip_x ? px : (7 - px);
            const uint8_t color_id =
                uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            if (color_id == 0) continue;

            const int screen_x = pos_x + px;
            if (screen_x < 0 || screen_x >= kScreenWidth) continue;

            // BG master priority (attr bit7 of BG) blocks sprites unless
            // LCDC.0 is clear (obj_master) or the BG pixel is colour 0.
            if (!obj_master && (prio_line[screen_x] & 0x04) &&
                (prio_line[screen_x] & 0x08)) {
                continue;
            }

            if (pri_mask == 0x80) {
                // Behind BG: only draw over colour-0 BG.
                if (prio_line[screen_x] & 0x08) continue;
            }

            row[screen_x] = rgb555_to_argb(spc_pal_[(color_id + pal) & 0x1F]);
            prio_line[screen_x] |= 0x01;
        }
    }
}

void GameBoy::render_scanline_gbc() {
    uint32_t* row = framebuffer_.data() + size_t(ly_) * kScreenWidth;
    uint8_t prio_line[160];
    std::fill(prio_line, prio_line + 160, 0);
    // Clear to colour 0 of BGP palette 0.
    const uint32_t blank = rgb555_to_argb(bgc_pal_[0]);
    for (int x = 0; x < kScreenWidth; x++) row[x] = blank;

    const bool obj_on = (lcd_control_ & 0x02) != 0 && !oam_dma_;
    uint8_t sprites[10];
    int sprite_count = 0;
    if (obj_on) get_active_sprites(sprites, &sprite_count, true);

    // Pass 0: BG + window
    plot_bg_line_gbc(row, prio_line, 0);
    plot_window_line_gbc(row, prio_line, 0);
    // Sprites behind
    if (obj_on) plot_sprites_line_gbc(row, prio_line, sprites, sprite_count, 0x80);
    // Pass 1
    plot_bg_line_gbc(row, prio_line, 1);
    plot_window_line_gbc(row, prio_line, 1);
    // Sprites front
    if (obj_on) plot_sprites_line_gbc(row, prio_line, sprites, sprite_count, 0x00);

    if ((lcd_control_ & 0x20) != 0 && ly_ >= window_y_ && window_x_ != 0 &&
        window_x_ <= 166) {
        window_y_draw_ = uint8_t(window_y_draw_ + 1);
    }
    std::copy(prio_line, prio_line + 160, line_prio_.begin());
}

void GameBoy::do_hdma_block(int bytes) {
    for (int i = 0; i < bytes; i++) {
        const uint8_t v = read_byte(hdma_src_);
        // Destination is always VRAM ($8000-$9FFF), bank selected by VBK.
        vram_[vram_bank_ & 1][hdma_dst_ & 0x1FFF] = v;
        hdma_src_ = uint16_t(hdma_src_ + 1);
        hdma_dst_ = uint16_t((hdma_dst_ + 1) & 0x1FFF);
    }
}


void GameBoy::run_frame() {
    window_y_draw_ = 0;
    for (ly_ = 0; ly_ < kScanlines; ly_++) {
        line_cycles_ = 0;

        // Mode 2 at start of visible lines, mode 1 at LY=144.
        update_lcd_stat(0);

        // Run one scanline worth of CPU.
        // The cycle handler advances timers; we track line_cycles_ here.
        // In GBC double-speed the CPU runs twice as many T-states per line.
        const int line_budget = kCyclesPerLine << cpu_.speed();
        int remaining = line_budget;
        while (remaining > 0) {
            const int ran = cpu_.run(remaining);
            line_cycles_ += ran;
            remaining -= ran;
            if (ran <= 0) break;
        }
        line_cycles_ = line_budget;

        if (ly_ == 144) {
            cpu_.request_vblank();
            update_lcd_stat(0);
        }

        if (ly_ < kScreenHeight && lcd_enabled_) {
            render_scanline();
            // HBlank DMA: 16 bytes per visible line while active.
            if (is_gbc_ && hdma_active_ && lcd_enabled_) {
                do_hdma_block(0x10);
                if (hdma_size_ == 0) {
                    hdma_active_ = false;
                    hdma_size_ = 0xFF;
                } else {
                    hdma_size_ = uint8_t(hdma_size_ - 1);
                }
            }
        }

        update_lcd_stat(line_cycles_);
    }
    ly_ = 0;
}

// ---------------------------------------------------------------------------
// Inputs / audio / DIP
// ---------------------------------------------------------------------------

void GameBoy::set_inputs(const MachineInputs& inputs) {
    // Active-low nibble layout matching eventos_gb:
    //   bit0 Right, bit1 Left, bit2 Up, bit3 Down,
    //   bit4 A, bit5 B, bit6 Select, bit7 Start
    uint8_t buttons = 0xFF;
    if (inputs.player1.right) buttons &= ~0x01;
    if (inputs.player1.left) buttons &= ~0x02;
    if (inputs.player1.up) buttons &= ~0x04;
    if (inputs.player1.down) buttons &= ~0x08;
    if (inputs.player1.button1) buttons &= ~0x10;  // A
    if (inputs.player1.button2) buttons &= ~0x20;  // B
    if (inputs.coin1) buttons &= ~0x40;            // Select
    if (inputs.player1.start) buttons &= ~0x80;    // Start

    if (buttons != joy_buttons_) {
        joy_buttons_ = buttons;
        cpu_.request_joystick();
    }
}

void GameBoy::set_dip_switch(int bank, uint8_t value) {
    // bank 0: palette (0 = green, 1 = grey)
    if (bank == 0) set_palette(value == 0 ? Palette::Green : Palette::Grey);
}

void GameBoy::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
