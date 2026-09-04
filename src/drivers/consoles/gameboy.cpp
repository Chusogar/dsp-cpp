#include "drivers/consoles/gameboy.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

bool read_plain_or_zip_file(const std::string& path, std::vector<uint8_t>& data, size_t max_size,
                            std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                     magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            probe.clear();
            probe.seekg(0, std::ios::end);
            std::streamoff size = probe.tellg();
            probe.seekg(0, std::ios::beg);
            if (size <= 0) return false;
            data.resize(size_t(size));
            probe.read(reinterpret_cast<char*>(data.data()), size);
            return bool(probe);
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    data.reserve(max_size);
    return loader.load_first_file(data, error);
}

// The 48-byte Nintendo logo bitmap stored at cartridge header offset $0104,
// from abrir_gb's main_logo constant. Used only to detect "unlicensed" carts
// that skip it (Wisdom Tree, some homebrew) -- purely a presence check, not
// reproduced anywhere the user can see it.
const std::array<uint8_t, 0x30> kNintendoLogo = {
    0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
    0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
    0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
};

}  // namespace

GameBoy::GameBoy() : cpu_(kClock) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
}

bool GameBoy::init(const std::string& rom_path, std::string* error) {
    // Boot ROMs are optional and, unlike every other system ported so far,
    // copyrighted and not freely distributable, so this looks for them but
    // does not require them; reset() falls back to the documented post-boot
    // register/IO state (matches reset_gb's `if not(rom_exist)` branch).
    namespace fs = std::filesystem;
    std::error_code ec;
    bool used_as_cart = false;
    if (!rom_path.empty() && fs::is_directory(rom_path, ec)) {
        std::string ignored_error;
        read_plain_or_zip_file((fs::path(rom_path) / "dmg_boot.bin").string(), dmg_boot_rom_,
                               0x100, &ignored_error);
        read_plain_or_zip_file((fs::path(rom_path) / "cgb_boot.bin").string(), cgb_boot_rom_,
                               0x900, &ignored_error);
    } else if (!rom_path.empty() && fs::is_regular_file(rom_path, ec)) {
        // No boot-ROM directory concept applies to a bare file: treat the
        // positional argument as the cartridge itself, for convenience
        // (`--game gb game.gb`), matching the SG-1000 driver.
        std::string cart_error;
        used_as_cart = load_media(rom_path, &cart_error);
        if (!used_as_cart) warnings_.push_back(cart_error);
    }
    (void)error;
    if (!used_as_cart) reset();
    return true;
}

bool GameBoy::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;
    if (data.size() < 0x150) {
        if (error) *error = "not a valid Game Boy cartridge (too small)";
        return false;
    }

    save_cart_ram();

    unlicensed_ = !std::equal(kNintendoLogo.begin(), kNintendoLogo.end(), data.begin() + 0x104);
    uint8_t cgb_flag = data[0x143];
    // gb_change_model((gb_head.cgb_flag and $80)<>0, ...): any cart with
    // bit 7 set is Game Boy Color, both $80 (enhanced) and $C0 (exclusive).
    is_cgb_ = (cgb_flag & 0x80) != 0;
    uint8_t header_type = data[0x147];
    uint8_t ram_size_code = data[0x149];

    // Wisdom Tree unlicensed carts and a handful of blank-header homebrew
    // ROMs identify as mapper 0 with no header despite being >32 KiB; not
    // implemented (see gb_mapper.h), so just report it clearly.
    bool blank_header_oversized = header_type == 0 && data[0x148] == 0 && data.size() > 32768;
    if (unlicensed_ && blank_header_oversized) {
        if (error) *error = "Wisdom Tree / blank-header oversized carts are not supported";
        return false;
    }

    uint32_t crc = crc32_of(data.data(), data.size());
    if (!mapper_.configure(header_type, crc, data.size(), ram_size_code)) {
        if (error) *error = "mapper $" + std::to_string(header_type) + " is not supported";
        return false;
    }
    mapper_.set_rom(std::move(data));
    load_cart_ram();
    reset();
    return true;
}

void GameBoy::save_cart_ram() {
    if (!mapper_.has_battery() || mapper_.ram().empty()) return;
    // Kept as a hook for a host-provided save path; dsp-cpp's Machine
    // interface has no notion of a save directory, so callers that want
    // persistence should read `mapper_.ram()` via their own means. Left
    // as a no-op placeholder rather than guessing a path.
}
void GameBoy::load_cart_ram() {}

void GameBoy::reset() {
    cpu_.reset();
    apu_.reset();
    ppu_.reset(is_cgb_);
    mapper_.reset();
    for (auto& bank : wram_) bank.fill(0);
    io_ram_.fill(0);
    wram_bank_ = 1;
    div_counter_ = 0;
    tima_ = tma_ = tac_ = 0;
    timer_cycles_ = 0;
    stat_ = 0;
    ly_compare_ = 0xff;
    line_ = 0;
    line_cycles_ = 0;
    sprites_time_ = 0;
    oam_dma_remaining_ = 0;
    dma_src_ = dma_dst_ = 0;
    hdma_active_ = false;
    hdma_size_ = 0xff;
    cgb_oam_unused_.fill(0);
    joy_select_ = 0x30;
    joy_val_ = 0xff;
    joystick_ = 0xff;
    audio_accumulator_ = 0;
    audio_.clear();
    ppu_.set_lcdc(0x80);
    ppu_.reset_window_line();

    const std::vector<uint8_t>& boot = is_cgb_ ? cgb_boot_rom_ : dmg_boot_rom_;
    boot_rom_enabled_ = !boot.empty();
    if (boot_rom_enabled_) return;  // CPU starts at $0000 and runs the real boot ROM

    apply_post_boot_state();
}

void GameBoy::apply_post_boot_state() {
    // Matches reset_gb's `if not(rom_exist)` branch: the documented
    // register/IO state a real boot ROM leaves behind, applied directly so
    // the game starts at $0100 without needing the (copyrighted) boot ROM.
    cpu_.pc = 0x100;
    cpu_.sp = 0xfffe;
    cpu_.fz = true;
    cpu_.fn = false;
    if (is_cgb_) {
        // A = $11 is how every CGB-aware cartridge detects Game Boy Color
        // hardware, so it must be the CGB value, not the DMG one.
        cpu_.a = 0x11;
        cpu_.fh = false;
        cpu_.fc = false;
        cpu_.b = 0x00; cpu_.c = 0x00;
        cpu_.d = 0xff; cpu_.e = 0x56;
        cpu_.h = 0x00; cpu_.l = 0x0d;
    } else {
        cpu_.a = 0x01;
        cpu_.fh = true;
        cpu_.fc = true;
        cpu_.b = 0x00; cpu_.c = 0x13;
        cpu_.d = 0x00; cpu_.e = 0xd8;
        cpu_.h = 0x01; cpu_.l = 0x4d;
    }

    // The post-boot I/O state is the same on both models.
    write_io(0x05, 0x00);
    write_io(0x06, 0x00);
    write_io(0x07, 0x00);
    write_io(0x10, 0x80);
    write_io(0x11, 0xbf);
    write_io(0x12, 0xf3);
    write_io(0x14, 0xbf);
    write_io(0x16, 0x3f);
    write_io(0x17, 0x00);
    write_io(0x19, 0xbf);
    write_io(0x1a, 0x7f);
    write_io(0x1b, 0x0f);
    write_io(0x1c, 0x9f);
    write_io(0x1e, 0xbf);
    write_io(0x20, 0xff);
    write_io(0x21, 0x00);
    write_io(0x22, 0x00);
    write_io(0x23, 0xbf);
    write_io(0x24, 0x77);
    write_io(0x25, 0xf3);
    write_io(0x26, 0xf1);
    write_io(0x40, 0x91);
    write_io(0x42, 0x00);
    write_io(0x43, 0x00);
    write_io(0x45, 0x00);
    write_io(0x47, 0xfc);
    write_io(0x48, 0xff);
    write_io(0x49, 0xff);
    write_io(0x4a, 0x00);
    write_io(0x4b, 0x00);
    write_io(0x00, 0x00);
    boot_rom_enabled_ = false;
}

uint8_t GameBoy::read_cgb_boot(uint16_t address) const {
    // gb.pas maps BIOS at $0000-$00FF and $0200-$08FF; $0100-$01FF is always
    // the cart. A 2304-byte (0x900) cgb_boot.bin already has that hole, so
    // index it by CPU address. Packed 256+1792 dumps concatenate the two
    // Pascal files (gbc_boot.1 @0, gbc_boot.2 @$200) and need remapping.
    const std::vector<uint8_t>& boot = cgb_boot_rom_;
    if (boot.empty()) return 0xff;
    if (boot.size() >= 0x900) {
        if (address < boot.size()) return boot[address];
        return 0xff;
    }
    size_t off = address < 0x100 ? address : size_t(0x100 + (address - 0x200));
    if (off < boot.size()) return boot[off];
    return 0xff;
}

uint8_t GameBoy::dma_read(uint16_t address) const {
    // dma_trans / OAM DMA source in gb.pas: VRAM reads as $FF, $E000-$FFFF
    // goes to cart RAM rather than the WRAM echo.
    if (address <= 0x7fff) return mapper_.read_rom(address);
    if (address <= 0x9fff) return 0xff;
    if (address <= 0xbfff) return mapper_.read_ram(uint16_t(address & 0x1fff));
    if (address <= 0xcfff) return wram_[0][address & 0xfff];
    if (address <= 0xdfff) return wram_[size_t(wram_bank_)][address & 0xfff];
    return mapper_.read_ram(uint16_t(address & 0x1fff));
}

uint8_t GameBoy::read_byte(uint16_t address) {
    if (address <= 0x7fff) {
        if (boot_rom_enabled_) {
            if (!is_cgb_) {
                const std::vector<uint8_t>& boot = dmg_boot_rom_;
                if (address < boot.size()) return boot[address];
            } else if (address < 0x100 || (address >= 0x200 && address < 0x900)) {
                return read_cgb_boot(address);
            }
        }
        return mapper_.read_rom(address);
    }
    if (address <= 0x9fff) return ppu_.vram_read(uint16_t(address - 0x8000));
    if (address <= 0xbfff) return mapper_.read_ram(uint16_t(address - 0xa000));
    if (address <= 0xcfff) return wram_[0][address & 0xfff];
    if (address <= 0xdfff) return wram_[size_t(wram_bank_)][address & 0xfff];
    if (address <= 0xefff) return wram_[0][address & 0xfff];        // echo
    if (address <= 0xfdff) return wram_[size_t(wram_bank_)][address & 0xfff];  // echo
    if (address <= 0xfe9f) return ppu_.oam_read(uint8_t(address & 0xff));
    if (address <= 0xfeff) {
        if (!is_cgb_) return 0;
        uint8_t lo = uint8_t(address & 0xff);
        if (lo <= 0xcf) return cgb_oam_unused_[lo - 0xa0];
        return cgb_oam_unused_[0x20 + (lo & 0x0f)];  // $FEC0 + (addr & $F)
    }
    return read_io(uint8_t(address & 0xff));
}

void GameBoy::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) { mapper_.write_register(address, value); return; }
    if (address <= 0x9fff) { ppu_.vram_write(uint16_t(address - 0x8000), value); return; }
    if (address <= 0xbfff) { mapper_.write_ram(uint16_t(address - 0xa000), value); return; }
    if (address <= 0xcfff) { wram_[0][address & 0xfff] = value; return; }
    if (address <= 0xdfff) { wram_[size_t(wram_bank_)][address & 0xfff] = value; return; }
    if (address <= 0xefff) { wram_[0][address & 0xfff] = value; return; }
    if (address <= 0xfdff) { wram_[size_t(wram_bank_)][address & 0xfff] = value; return; }
    if (address <= 0xfe9f) { ppu_.oam_write(uint8_t(address & 0xff), value); return; }
    if (address <= 0xfeff) {
        if (!is_cgb_) return;
        uint8_t lo = uint8_t(address & 0xff);
        if (lo <= 0xcf) cgb_oam_unused_[lo - 0xa0] = value;
        else cgb_oam_unused_[0x20 + (lo & 0x0f)] = value;
        return;
    }
    write_io(uint8_t(address & 0xff), value);
}

uint8_t GameBoy::read_io(uint8_t offset) {
    switch (offset) {
        case 0x00:
            if ((joy_select_ & 0x10) == 0) joystick_ = uint8_t((joystick_ | 0xf) & (joy_val_ | 0xf0));
            if ((joy_select_ & 0x20) == 0) joystick_ = uint8_t((joystick_ | 0xf) & ((joy_val_ >> 4) | 0xf0));
            return joystick_;
        case 0x01: return 0;
        case 0x02: return is_cgb_ ? 0x7c : 0x7e;
        case 0x04: return uint8_t(div_counter_ >> 8);
        case 0x05: return tima_;
        case 0x06: return tma_;
        case 0x07: return uint8_t(0xf8 | tac_);
        case 0x0f: {
            uint8_t v = 0xe0;
            if (cpu_.vblank_req) v |= 0x01;
            if (cpu_.lcdstat_req) v |= 0x02;
            if (cpu_.timer_req) v |= 0x04;
            if (cpu_.serial_req) v |= 0x08;
            if (cpu_.joystick_req) v |= 0x10;
            return v;
        }
        case 0x40: return ppu_.lcdc();
        case 0x41: return uint8_t(0x80 | stat_);
        case 0x42: return io_ram_[0x42];
        case 0x43: return io_ram_[0x43];
        case 0x44: return uint8_t(line_);
        case 0x45: return ly_compare_;
        case 0x47: return io_ram_[0x47];
        case 0x48: return io_ram_[0x48];
        case 0x49: return io_ram_[0x49];
        case 0x4a: return io_ram_[0x4a];
        case 0x4b: return io_ram_[0x4b];
        // The CGB-only registers do not exist on DMG and read back as $FF.
        case 0x4d:
            return is_cgb_ ? uint8_t((cpu_.speed << 7) | 0x7e | (cpu_.change_speed ? 1 : 0)) : 0xff;
        case 0x4f: return is_cgb_ ? ppu_.vbk() : 0xff;
        case 0x51: case 0x52: case 0x53: case 0x54: return 0xff;
        case 0x55: return is_cgb_ ? hdma_size_ : 0xff;
        case 0x56: return is_cgb_ ? 1 : 0xff;
        case 0x68: return is_cgb_ ? ppu_.bg_pal_index() : 0xff;
        case 0x69: return is_cgb_ ? ppu_.read_bg_pal_data() : 0xff;
        case 0x6a: return is_cgb_ ? ppu_.obj_pal_index() : 0xff;
        case 0x6b: return is_cgb_ ? ppu_.read_obj_pal_data() : 0xff;
        case 0x70: return is_cgb_ ? uint8_t(0xf8 | wram_bank_) : 0xff;
        case 0xff: return io_ram_[0xff];
        default:
            if (offset >= 0x10 && offset <= 0x26) return apu_.read(uint8_t(offset - 0x10));
            if (offset >= 0x30 && offset <= 0x3f) return apu_.wave_read(uint8_t(offset - 0x30));
            return io_ram_[offset];
    }
}

void GameBoy::write_io(uint8_t offset, uint8_t value) {
    if (io_write_hook_) io_write_hook_(offset, value);
    io_ram_[offset] = value;
    switch (offset) {
        case 0x00: joy_select_ = uint8_t(0xcf | (value & 0x30)); joystick_ = joy_select_; break;
        case 0x02: if ((value & 0x81) == 0x81) cpu_.serial_req = true; break;
        case 0x04: div_counter_ = 0; break;
        case 0x05: tima_ = value; break;
        case 0x06: tma_ = value; break;
        case 0x07: tac_ = value & 7; break;
        case 0x0f:
            cpu_.vblank_req = (value & 0x01) != 0;
            cpu_.lcdstat_req = (value & 0x02) != 0;
            cpu_.timer_req = (value & 0x04) != 0;
            cpu_.serial_req = (value & 0x08) != 0;
            cpu_.joystick_req = (value & 0x10) != 0;
            break;
        case 0x40: {
            uint8_t old = ppu_.lcdc();
            ppu_.set_lcdc(value);
            if ((value & 0x80) == 0) stat_ &= 0xfc;
            (void)old;
            break;
        }
        case 0x41: stat_ = uint8_t((stat_ & 0x7) | (value & 0xf8)); break;
        case 0x42: {
            int sample = (line_cycles_ >> cpu_.speed) / 4;
            ppu_.write_scy_mid_line(value, sample);
            break;
        }
        case 0x43: ppu_.set_scx(value); break;
        case 0x45: ly_compare_ = value; break;
        case 0x46: {  // OAM DMA
            uint16_t src = uint16_t(value << 8);
            for (int i = 0; i < 0xa0; i++) ppu_.oam_write(uint8_t(i), dma_read(uint16_t(src + i)));
            oam_dma_remaining_ = 160;
            break;
        }
        case 0x47: ppu_.set_bgp(value); break;
        case 0x48: ppu_.set_obp0(value); break;
        case 0x49: ppu_.set_obp1(value); break;
        case 0x4a: ppu_.set_wy(value); break;
        case 0x4b: ppu_.set_wx(value); break;
        case 0x4d: if (is_cgb_) cpu_.change_speed = (value & 1) != 0; break;
        case 0x4f: if (is_cgb_) ppu_.set_vbk(value); break;
        case 0x50: boot_rom_enabled_ = false; break;
        case 0x51: dma_src_ = uint16_t((dma_src_ & 0xff) | (value << 8)); break;
        case 0x52: dma_src_ = uint16_t((dma_src_ & 0xff00) | (value & 0xf0)); break;
        case 0x53: dma_dst_ = uint16_t((dma_dst_ & 0xff) | ((value & 0x1f) << 8)); break;
        case 0x54: dma_dst_ = uint16_t((dma_dst_ & 0xff00) | (value & 0xf0)); break;
        case 0x55:
            if (!is_cgb_) break;
            if ((value & 0x80) != 0) {
                hdma_size_ = value & 0x7f;
                hdma_active_ = true;
            } else if (hdma_active_) {
                // Clearing bit 7 while an HBlank transfer runs aborts it, and
                // the remaining length stays readable with bit 7 set.
                hdma_active_ = false;
                hdma_size_ = uint8_t(hdma_size_ | 0x80);
            } else {
                int blocks = value + 1;
                int len = blocks * 0x10;
                for (int i = 0; i < len; i++) {
                    ppu_.vram_write(uint16_t(dma_dst_ & 0x1fff), dma_read(dma_src_));
                    dma_dst_++;
                    dma_src_++;
                }
                hdma_size_ = 0xff;
                // lr35902.pas estados_demas: (220 shr speed) + 8*(valor+1)
                cpu_.add_stall_cycles((220 >> cpu_.speed) + 8 * blocks);
            }
            break;
        case 0x68: if (is_cgb_) ppu_.set_bg_pal_index(value); break;
        case 0x69: if (is_cgb_) ppu_.write_bg_pal_data(value, (stat_ & 3) == 3); break;
        case 0x6a: if (is_cgb_) ppu_.set_obj_pal_index(value); break;
        case 0x6b: if (is_cgb_) ppu_.write_obj_pal_data(value, (stat_ & 3) == 3); break;
        case 0x70:
            if (!is_cgb_) break;
            wram_bank_ = value & 7;
            if (wram_bank_ == 0) wram_bank_ = 1;
            break;
        case 0xff:
            cpu_.vblank_ena = (value & 0x01) != 0;
            cpu_.lcdstat_ena = (value & 0x02) != 0;
            cpu_.timer_ena = (value & 0x04) != 0;
            cpu_.serial_ena = (value & 0x08) != 0;
            cpu_.joystick_ena = (value & 0x10) != 0;
            break;
        default:
            if (offset >= 0x10 && offset <= 0x26) apu_.write(uint8_t(offset - 0x10), value);
            else if (offset >= 0x30 && offset <= 0x3f) apu_.wave_write(uint8_t(offset & 0xf), value);
            break;
    }
}

void GameBoy::step_div(int cycles) { div_counter_ = uint16_t(div_counter_ + cycles); }

void GameBoy::step_timer(int cycles) {
    if ((tac_ & 4) == 0) return;
    static constexpr int kPeriods[4] = {1024, 16, 64, 256};
    timer_cycles_ += cycles;
    int period = kPeriods[tac_ & 3];
    while (timer_cycles_ >= period) {
        timer_cycles_ -= period;
        tima_++;
        if (tima_ == 0) {
            tima_ = tma_;
            cpu_.timer_req = true;
        }
    }
}

void GameBoy::step_oam_dma(int cycles) {
    if (oam_dma_remaining_ <= 0) return;
    int step = is_cgb_ ? (cycles >> cpu_.speed) : cycles;
    oam_dma_remaining_ -= step;
}

void GameBoy::run_line_checkpoint_zero() {
    bool lcd_compare = false, lcd_mode = false;
    if (line_ == ly_compare_) {
        lcd_compare = (stat_ & 0x40) != 0;
        stat_ |= 0x04;
    } else {
        stat_ &= 0xfb;
    }
    if (line_ < 144) {
        lcd_mode = (stat_ & 0x20) != 0;
        stat_ = uint8_t((stat_ & 0xfc) | 0x02);
    }
    if (line_ == 144) {
        lcd_mode = (stat_ & 0x10) != 0;
        stat_ = uint8_t((stat_ & 0xfc) | 0x01);
    }
    if (lcd_compare || lcd_mode) cpu_.lcdstat_req = true;
    hdma_done_this_line_ = false;
}

void GameBoy::on_cpu_cycles(int cycles) {
    step_div(cycles);
    step_timer(cycles);
    step_oam_dma(cycles);

    // gbc_despues_instruccion runs this WRAM poke every instruction, LCD on or off.
    if (is_cgb_ && wram_[0][0x1a4] == wram_[1][0x1a4]) wram_[0][0x1a4] = 0xed;
    if (cpu_.changed_speed) cpu_.changed_speed = false;

    // The APU keeps running in real time while the CGB doubles the CPU clock,
    // so samples are produced per single-speed cycle.
    audio_accumulator_ += uint64_t(cycles >> cpu_.speed) * uint64_t(GbApu::kSampleRate);
    while (audio_accumulator_ >= kClock) {
        audio_accumulator_ -= kClock;
        audio_.push_back(apu_.update());
    }

    if ((ppu_.lcdc() & 0x80) == 0) {
        // With the LCD off there is no HBlank to wait for, so an HBlank DMA
        // keeps copying instead of stalling until the display comes back.
        if (is_cgb_ && hdma_active_) hdma_block();
        return;
    }

    int prev = line_cycles_;
    int cur = prev + cycles;
    int div_speed = 1 << cpu_.speed;
    int prev_c = prev / div_speed;
    int cur_c = cur / div_speed;

    bool lcd_mode = false;
    if (prev_c < 80 && cur_c >= 80) {
        if (line_ < 144) {
            lcd_mode = ((stat_ & 0x20) != 0) && ((stat_ & 0x10) == 0);
            stat_ = uint8_t((stat_ & 0xfc) | 0x03);
        }
        if (line_ == 144) cpu_.vblank_req = true;
    }
    if (line_ < 144 && cur_c >= 248 && cur_c <= 600) {
        if (is_cgb_ && !hdma_done_this_line_ && cur_c >= 300 && hdma_active_) {
            hdma_done_this_line_ = true;
            hdma_block();
            cur += 8;  // gb.pas adds 8 to contador after each HDMA block
            cur_c = cur / div_speed;
        }
        int threshold = sprites_time_ + 248;
        if (prev_c < threshold && cur_c >= threshold && (stat_ & 3) != 0) {
            lcd_mode = ((stat_ & 0x08) != 0) && ((stat_ & 0x20) == 0);
            stat_ &= 0xfc;
        }
    }
    if (lcd_mode) cpu_.lcdstat_req = true;
    line_cycles_ = cur;
}

void GameBoy::hdma_block() {
    for (int i = 0; i < 0x10; i++) {
        ppu_.vram_write(uint16_t(dma_dst_ & 0x1fff), dma_read(dma_src_));
        dma_dst_++;
        dma_src_++;
    }
    if (hdma_size_ == 0) { hdma_size_ = 0xff; hdma_active_ = false; }
    else hdma_size_--;
}

void GameBoy::run_frame() {
    for (line_ = 0; line_ < kScanlines; line_++) {
        line_cycles_ = 0;
        if ((ppu_.lcdc() & 0x80) != 0) run_line_checkpoint_zero();
        cpu_.run(kCyclesPerLine << cpu_.speed);
        if (line_ < 144) {
            ppu_.set_oam_dma(oam_dma_remaining_ > 0);
            ppu_.render_scanline(line_, &framebuffer_[size_t(line_) * GbPpu::kScreenWidth]);
            if ((ppu_.lcdc() & 0x80) != 0) {
                if ((ppu_.lcdc() & 2) != 0 && oam_dma_remaining_ <= 0) {
                    sprites_time_ = ppu_.sprite_mode3_penalty(line_, cpu_.speed);
                }
                if (ppu_.window_visible_on(line_)) ppu_.advance_window_line();
            }
        }
    }
    ppu_.reset_window_line();
}

void GameBoy::set_inputs(const MachineInputs& inputs) {
    const InputState& p1 = inputs.player1;
    uint8_t v = 0xff;
    if (p1.right) v &= 0xfe;
    if (p1.left) v &= 0xfd;
    if (p1.up) v &= 0xfb;
    if (p1.down) v &= 0xf7;
    if (p1.button1) v &= 0xef;   // A
    if (p1.button2) v &= 0xdf;   // B
    if (p1.button3) v &= 0xbf;   // Select
    if (p1.start) v &= 0x7f;     // Start
    joy_val_ = v;
}

void GameBoy::set_dip_switch(int, uint8_t) {
    // Cartridge console, no DIP switches.
}

void GameBoy::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
