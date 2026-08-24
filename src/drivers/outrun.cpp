#include "drivers/outrun.h"

#include <algorithm>

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRom = {
    {"epr-10380b.133", 0x10000, 0x00000, 0x1f6cadad},
    {"epr-10382b.118", 0x10000, 0x00001, 0xc4c3fa1a},
    {"epr-10381b.132", 0x10000, 0x20000, 0xbe8c412b},
    {"epr-10383b.117", 0x10000, 0x20001, 0x10a2014a},
};
const std::vector<RomEntry> kSubRom = {
    {"epr-10327a.76", 0x10000, 0x00000, 0xe28a5baf},
    {"epr-10329a.58", 0x10000, 0x00001, 0xda131c81},
    {"epr-10328a.75", 0x10000, 0x20000, 0xd5ec5e5d},
    {"epr-10330a.57", 0x10000, 0x20001, 0xba9ec82a},
};
const std::vector<RomEntry> kSoundRom = {{"epr-10187.88", 0x8000, 0, 0xa10abaa9}};
const std::vector<RomEntry> kTileRom = {
    {"opr-10268.99", 0x8000, 0x00000, 0x95344b04},
    {"opr-10232.102", 0x8000, 0x08000, 0x776ba1eb},
    {"opr-10267.100", 0x8000, 0x10000, 0xa85bb823},
    {"opr-10231.103", 0x8000, 0x18000, 0x8908bcbf},
    {"opr-10266.101", 0x8000, 0x20000, 0x9f6f1a74},
    {"opr-10230.104", 0x8000, 0x28000, 0x686f5e50},
};
const std::vector<RomEntry> kSpriteRom = {
    {"mpr-10371.9", 0x20000, 0x00000, 0x7cc86208},
    {"mpr-10373.10", 0x20000, 0x00001, 0xb0d26ac9},
    {"mpr-10375.11", 0x20000, 0x00002, 0x59b60bd7},
    {"mpr-10377.12", 0x20000, 0x00003, 0x17a1b04a},
    {"mpr-10372.13", 0x20000, 0x80000, 0xb557078c},
    {"mpr-10374.14", 0x20000, 0x80001, 0x8051e517},
    {"mpr-10376.15", 0x20000, 0x80002, 0xf3b8f318},
    {"mpr-10378.16", 0x20000, 0x80003, 0xa1062984},
};
const std::vector<RomEntry> kRoadRom = {
    {"opr-10186.47", 0x8000, 0x0000, 0x22794426},
    {"opr-10185.11", 0x8000, 0x8000, 0x22794426},
};
const std::vector<RomEntry> kPcmRom = {
    {"opr-10193.66", 0x8000, 0x00000, 0xbcd10dde},
    {"opr-10192.67", 0x8000, 0x10000, 0x770f1270},
    {"opr-10191.68", 0x8000, 0x20000, 0x20a284ab},
    {"opr-10190.69", 0x8000, 0x30000, 0x7cab70e2},
    {"opr-10189.70", 0x8000, 0x40000, 0x01366b54},
    {"opr-10188.71", 0x8000, 0x50000, 0xbad30ad9},
};

}  // namespace


Outrun::Outrun()
    : main_cpu_(kMainClock),
      sub_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(4000000),
      pcm_(4000000, 1.0f),
      framebuffer_(kScreenWidth * kScreenHeight, 0),
      road_fg_(kScreenWidth * kScreenHeight, 0) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers([this](uint32_t a) { return sub_read(a); },
                                 [this](uint32_t a, uint16_t v) { sub_write(a, v); });
    sub_cpu_.set_byte_handlers([this](uint32_t a) { return sub_read_byte(a); },
                               [this](uint32_t a, uint8_t v) { sub_write_byte(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    pcm_.set_read_rom([this](uint32_t addr) -> uint8_t {
        if (pcm_rom_.empty()) return 0x80;
        return pcm_rom_[addr % pcm_rom_.size()];
    });
    pcm_.set_bank(SegaPcm::kBank512);
    mapper_.set_sound_latch([this](uint8_t value) {
        sound_latch_ = value;
        sound_cpu_.set_nmi(IrqLine::Assert);
    });
    mapper_.set_bus_handlers([this](uint32_t a) { return main_read(a); },
                             [this](uint32_t a, uint16_t v) { main_write(a, v); });
    mapper_.set_reset_handler([this](IrqLine state) {
        main_cpu_.set_reset_line(state);
        if (state == IrqLine::Assert) sub_cpu_.set_reset_line(IrqLine::Pulse);
    });
    mapper_.set_irq_handler([this](int level, IrqLine state) { main_cpu_.set_irq(level, state); });
    // Real hardware: the main CPU's RESET instruction pulses an external
    // reset line that the sub CPU is wired to (Pascal outrun_reset_cpu2).
    // It does not reset the main CPU itself.
    main_cpu_.set_reset_instruction_handler([this]() { sub_cpu_.set_reset_line(IrqLine::Pulse); });
    ppi_.set_port_handlers(nullptr, nullptr, nullptr, nullptr, nullptr, [this](uint8_t value) {
        video_.screen_enabled = (value & 0x20) != 0;
        adc_select_ = (value >> 2) & 7;
        z80_reset_ = (value & 1) == 0;
        if (z80_reset_) sound_cpu_.reset();
    });
}

bool Outrun::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    video_.init_palette_luts();
    reset();
    return true;
}

bool Outrun::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    if (!load_roms16w(loader, kMainRom, rom_, error)) return false;
    if (!load_roms16w(loader, kSubRom, rom2_, error)) return false;
    std::vector<uint8_t> sound;
    if (!load_rom_bytes(loader, kSoundRom, sound, error)) return false;
    std::fill(sound_rom_.begin(), sound_rom_.end(), 0);
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());
    std::vector<uint8_t> tiles;
    if (!load_rom_bytes(loader, kTileRom, tiles, error)) return false;
    decode_s16_tiles(video_.tiles, tiles, 2);
    if (!load_roms32dw(loader, kSpriteRom, sprite_rom_, error)) return false;
    sprite_banks_ = 4;
    std::vector<uint8_t> road;
    if (!load_rom_bytes(loader, kRoadRom, road, error)) return false;
    decode_outrun_road(road_gfx_, road);
    if (!load_rom_bytes(loader, kPcmRom, pcm_rom_, error)) return false;
    pcm_rom_.resize(0x60000, 0);
    for (int bank = 0; bank < 6; bank++) {
        std::copy(pcm_rom_.begin() + bank * 0x10000, pcm_rom_.begin() + bank * 0x10000 + 0x8000,
                  pcm_rom_.begin() + bank * 0x10000 + 0x8000);
    }
    return true;
}

void Outrun::reset() {
    mapper_.reset();
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    pcm_.reset();
    ppi_.reset();
    video_.reset();
    video_.screen_enabled = false;
    ram_.fill(0);
    cpu1ram_.fill(0);
    road_ram_.fill(0);
    road_buffer_.fill(0);
    in0_ = 0x00ef;
    adc_select_ = 0;
    sound_latch_ = 0;
    gear_hi_ = false;
    push_gear_ = false;
    z80_reset_ = false;
    road_control_ = 0;
    analog_x_ = 0x80;
    analog_gas_ = 0;
    analog_brake_ = 0;
    audio_acc_ = 0;
    pcm_acc_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
    std::fill(road_fg_.begin(), road_fg_.end(), 0);
}

void Outrun::set_inputs(const MachineInputs& inputs) {
    in0_ = uint16_t(0x00ef | (in0_ & 0x10));
    if (inputs.player1.start) in0_ &= ~0x0008;
    if (inputs.player1.button3) {
        push_gear_ = true;
    } else if (push_gear_) {
        gear_hi_ = !gear_hi_;
        if (gear_hi_) in0_ |= 0x10;
        else in0_ &= ~0x10;
        push_gear_ = false;
    }
    if (inputs.coin1) in0_ &= ~0x0040;
    if (inputs.coin2) in0_ &= ~0x0080;
    analog_x_ = 0x80;
    if (inputs.player1.left) analog_x_ = 0x20;
    if (inputs.player1.right) analog_x_ = 0xe0;
    analog_gas_ = (inputs.player1.up || inputs.player1.button1) ? 0xff : 0;
    analog_brake_ = (inputs.player1.down || inputs.player1.button2) ? 0xff : 0;
}

void Outrun::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void Outrun::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void Outrun::swap_road() {
    for (int i = 0; i < 0x800; i++) std::swap(road_ram_[size_t(i)], road_buffer_[size_t(i)]);
}

uint16_t Outrun::io_read(uint16_t address) {
    switch (address & 0x38) {
        case 0x00:
            return ppi_.read(address & 3);
        case 0x08:
            switch (address & 3) {
                case 0: return in0_;
                case 1: return 0xff;
                case 2: return dsw_a_;
                case 3: return dsw_b_;
            }
            break;
        case 0x18:
            switch (adc_select_) {
                case 0: return analog_x_;
                case 1: return analog_gas_;
                case 2: return analog_brake_;
                default: return 0xff;
            }
        default:
            break;
    }
    return 0xffff;
}

void Outrun::io_write(uint16_t address, uint16_t value) {
    if ((address & 0x38) == 0) ppi_.write(address & 3, uint8_t(value));
}

uint16_t Outrun::main_read(uint32_t address) {
    address &= 0xffffff;
    if (mapper_.contains(0, address)) {
        if (address <= 0x5ffff) return rom_[(address & 0x3ffff) >> 1];
        if (address >= 0x60000 && address <= 0x67fff) return ram_[(address & 0x7fff) >> 1];
        return 0xffff;
    }
    bool mapped = false;
    uint16_t result = 0xffff;
    if (mapper_.contains(1, address)) {
        switch (address & 0x1ffff) {
            case 0x00000 ... 0x0ffff:
                result = video_.tile_ram[(address & 0xffff) >> 1];
                break;
            default:
                result = video_.char_ram[(address & 0xfff) >> 1];
                break;
        }
        mapped = true;
    }
    if (mapper_.contains(2, address)) {
        result = video_.pal_ram[(address & 0x1fff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(3, address)) {
        result = video_.sprite_ram[(address & 0xfff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(4, address)) {
        result = io_read(uint16_t((address & 0x7f) >> 1));
        mapped = true;
    }
    if (mapper_.contains(5, address)) {
        switch (address & 0xfffff) {
            case 0x00000 ... 0x5ffff:
                result = rom2_[(address & 0x3ffff) >> 1];
                break;
            case 0x60000 ... 0x7ffff:
                // Sub CPU's own work RAM ("cpu1ram" in MAME), reached here
                // through the main CPU's region 5 window.
                result = cpu1ram_[(address & 0x7fff) >> 1];
                break;
            case 0x80000 ... 0x8ffff:
                result = road_ram_[(address & 0xfff) >> 1];
                break;
            case 0x90000 ... 0x9ffff:
                // Matches sub_read(): Pascal's outrun_getword also exposes
                // the road buffer swap trigger through this window.
                swap_road();
                result = 0xffff;
                break;
            default:
                result = 0xffff;
                break;
        }
        mapped = true;
    }
    if (!mapped) result = mapper_.read_reg(uint8_t((address >> 1) & 0x1f));
    return result;
}

void Outrun::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    bool mapped = false;
    if (mapper_.contains(0, address)) {
        if (address >= 0x60000 && address <= 0x67fff) ram_[(address & 0x7fff) >> 1] = value;
        mapped = true;
    }
    if (mapper_.contains(1, address)) {
        switch (address & 0x1ffff) {
            case 0x00000 ... 0x0ffff: {
                const uint16_t offset = uint16_t((address & 0xffff) >> 1);
                if (video_.tile_ram[offset] != value) {
                    video_.tile_ram[offset] = value;
                    video_.mark_tile(offset);
                }
                break;
            }
            default: {
                const uint16_t offset = uint16_t((address & 0xfff) >> 1);
                if (video_.char_ram[offset] != value) {
                    video_.char_ram[offset] = value;
                    video_.text_dirty[offset] = true;
                }
                video_.apply_screen_select_16b(offset);
                break;
            }
        }
        mapped = true;
    }
    if (mapper_.contains(2, address)) {
        const int index = int((address & 0x1fff) >> 1);
        video_.set_palette_entry(index, value, true, false);
        mapped = true;
    }
    if (mapper_.contains(3, address)) {
        video_.sprite_ram[(address & 0xfff) >> 1] = value;
        mapped = true;
    }
    if (mapper_.contains(4, address)) {
        io_write(uint16_t((address & 0x7f) >> 1), value);
        mapped = true;
    }
    if (mapper_.contains(5, address)) {
        switch (address & 0xfffff) {
            case 0x60000 ... 0x7ffff:
                cpu1ram_[(address & 0x7fff) >> 1] = value;
                break;
            case 0x80000 ... 0x8ffff:
                road_ram_[(address & 0xfff) >> 1] = value;
                break;
            case 0x90000 ... 0x9ffff:
                road_control_ = uint8_t(value & 3);
                break;
            default:
                break;
        }
        mapped = true;
    }
    if (!mapped) {
        const uint32_t old = mapper_.dirs_start(1);
        mapper_.write_reg(uint8_t((address >> 1) & 0x1f), uint8_t(value));
        if (old != mapper_.dirs_start(1)) {
            for (auto& page : video_.tile_dirty) page.fill(false);
        }
    }
}

uint16_t Outrun::sub_read(uint32_t address) {
    address &= 0xfffff;
    if (address <= 0x5ffff) return rom2_[(address & 0x3ffff) >> 1];
    if (address >= 0x60000 && address <= 0x7ffff) return cpu1ram_[(address & 0x7fff) >> 1];
    if (address >= 0x80000 && address <= 0x8ffff) return road_ram_[(address & 0xfff) >> 1];
    if (address >= 0x90000 && address <= 0x9ffff) {
        // Pascal's outrun_sub_getword triggers the road double-buffer swap
        // here (gated on the CPU's "reading the high byte" bus direction,
        // which a plain word read always satisfies). Our core dispatches
        // byte-sized CPU reads through a separate handler (sub_read_byte),
        // so this word-level path needs its own copy of the same trigger
        // or a word-sized trigger read is silently missed.
        swap_road();
        return 0xffff;
    }
    return 0xffff;
}

void Outrun::sub_write(uint32_t address, uint16_t value) {
    address &= 0xfffff;
    if (address >= 0x60000 && address <= 0x67fff) cpu1ram_[(address & 0x7fff) >> 1] = value;
    else if (address >= 0x80000 && address <= 0x8ffff) road_ram_[(address & 0xfff) >> 1] = value;
    else if (address >= 0x90000 && address <= 0x9ffff) road_control_ = uint8_t(value & 3);
}

uint8_t Outrun::sub_read_byte(uint32_t address) {
    address &= 0xfffff;
    if (address >= 0x90000 && address <= 0x9ffff) {
        // Pascal only swaps on the high-byte (even) half of the access
        // (m68000_1.read_8bits_hi_dir). A word read uses the word handler
        // (sub_read) and swaps once; an odd-byte read must not swap again.
        if ((address & 1) == 0) swap_road();
        return 0xff;
    }
    const uint16_t word = sub_read(address);
    return (address & 1) ? uint8_t(word) : uint8_t(word >> 8);
}

void Outrun::sub_write_byte(uint32_t address, uint8_t value) {
    address &= 0xfffff;
    if (address >= 0x90000 && address <= 0x9ffff) {
        road_control_ = uint8_t(value & 3);
        return;
    }
    uint16_t old = sub_read(address);
    if (address & 1) old = uint16_t((old & 0xff00) | value);
    else old = uint16_t((old & 0x00ff) | (uint16_t(value) << 8));
    sub_write(address, old);
}

uint8_t Outrun::sound_read(uint16_t address) {
    if (address >= 0xf000 && address <= 0xf7ff) return pcm_.read(address);
    return sound_rom_[address];
}

void Outrun::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0xf000 && address <= 0xf7ff) pcm_.write(address, value);
    else if (address >= 0xf800) sound_rom_[address] = value;
}

uint8_t Outrun::sound_in(uint16_t port) {
    switch (port & 0xff) {
        case 0x00 ... 0x3f:
            if (port & 1) return ym_.status();
            break;
        case 0x40 ... 0x7f:
            sound_cpu_.set_nmi(IrqLine::Clear);
            return sound_latch_;
        default:
            break;
    }
    return 0xff;
}

void Outrun::sound_out(uint16_t port, uint8_t value) {
    if ((port & 0xff) <= 0x3f) {
        if ((port & 1) == 0) ym_.select_register(value);
        else ym_.write(value);
    }
}

void Outrun::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);
    pcm_acc_ += int64_t(cycles) * pcm_.tick_rate();
    while (pcm_acc_ >= kSoundClock) {
        pcm_acc_ -= kSoundClock;
        pcm_.clock();
    }
    audio_acc_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_acc_ >= kSoundClock) {
        audio_acc_ -= kSoundClock;
        const int32_t sample = ym_.update() + pcm_.last_sample();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void Outrun::update_video() {
    // Pascal update_video_outrun: blank to paleta[$2000] when the PPI has not
    // enabled the screen. Attract sets that bit before it writes a live picture.
    if (!video_.screen_enabled) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), video_.palette[0x2000]);
        return;
    }
    video_.render_tile_pages(bg_low_, bg_high_, 0, true, 6, 0x1fff, 0x8000, false, false);
    video_.render_tile_pages(fg_low_, fg_high_, 4, true, 6, 0x1fff, 0x8000, false, false);
    video_.render_text(text_low_, text_high_, 9, 0x1ff, 0x8000, false);
    const int scroll_y1 = video_.char_ram[0x749] & 0x1ff;
    const int scroll_y2 = video_.char_ram[0x748] & 0x1ff;
    const bool row_back = (video_.char_ram[0x74d] & 0x8000) != 0;
    const bool row_fore = (video_.char_ram[0x74c] & 0x8000) != 0;
    const int scroll_x1 = row_back ? 0 : (704 - (video_.char_ram[0x74d] & 0x3ff)) & 0x3ff;
    const int scroll_x2 = row_fore ? 0 : (704 - (video_.char_ram[0x74c] & 0x3ff)) & 0x3ff;
    auto blit_layer = [&](const std::vector<uint32_t>& src, bool row, int sx, int sy,
                          uint16_t table_base) {
        if (row) video_.blit_rowscroll(framebuffer_.data(), src, sy, table_base);
        else video_.blit_scrolled(framebuffer_.data(), src, sx, sy, 1024, 512);
    };

    draw_outrun_road(framebuffer_.data(), video_.palette.data(), road_buffer_.data(),
                     road_gfx_.data(), road_control_, 0x400, 0x420, 0x780, 0, 0);
    blit_layer(bg_low_, row_back, scroll_x1, scroll_y1, 0x7e0);
    draw_sprites_outrun(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 0, 0x1000);
    blit_layer(bg_high_, row_back, scroll_x1, scroll_y1, 0x7e0);
    draw_sprites_outrun(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 1, 0x1000);
    blit_layer(fg_low_, row_fore, scroll_x2, scroll_y2, 0x7c0);
    draw_sprites_outrun(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 2, 0x1000);
    blit_layer(fg_high_, row_fore, scroll_x2, scroll_y2, 0x7c0);

    // Pascal draw_road(1) paints screen 8 with paleta[MAX_COLORES] (colour key),
    // then actualiza_trozo composites it onto screen 7. Direct drawing onto the
    // framebuffer overwrites tile/sprite pixels that should show through skipped
    // road lines (both data words have bit $800).
    constexpr uint32_t kRoadKey = 0x01000000u;
    std::fill(road_fg_.begin(), road_fg_.end(), kRoadKey);
    draw_outrun_road(road_fg_.data(), video_.palette.data(), road_buffer_.data(),
                     road_gfx_.data(), road_control_, 0x400, 0x420, 0x780, 0, 1);
    for (size_t i = 0; i < road_fg_.size(); i++) {
        if (road_fg_[i] != kRoadKey) framebuffer_[i] = road_fg_[i];
    }

    video_.blit_text(framebuffer_.data(), text_low_);
    draw_sprites_outrun(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 3, 0x1000);
    video_.blit_text(framebuffer_.data(), text_high_);
}

void Outrun::run_frame() {
    const int main_cycles =
        int(double(kMainClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sound_cycles =
        int(double(kSoundClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 65 || line == 129 || line == 193) main_cpu_.set_irq(2, IrqLine::Assert);
        if (line == 66 || line == 130 || line == 194) main_cpu_.set_irq(2, IrqLine::Clear);
        if (line == 223) {
            main_cpu_.set_irq(4, IrqLine::Assert);
            sub_cpu_.set_irq(4, IrqLine::Assert);
            update_video();
        }
        if (line == 224) {
            main_cpu_.set_irq(4, IrqLine::Clear);
            sub_cpu_.set_irq(4, IrqLine::Clear);
        }
        for (int slice = 0; slice < kCpuSync; slice++) {
            main_cpu_.run(main_cycles);
            sub_cpu_.run(main_cycles);
            if (!z80_reset_) sound_cpu_.run(sound_cycles);
        }
    }
}

}  // namespace dsp
