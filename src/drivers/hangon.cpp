#include "drivers/hangon.h"

#include <algorithm>

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRom = {
    {"epr-6918a.ic22", 0x8000, 0x00000, 0x20b1c2b0},
    {"epr-6916a.ic8", 0x8000, 0x00001, 0x7d9db1bf},
    {"epr-6917a.ic20", 0x8000, 0x10000, 0xfea12367},
    {"epr-6915a.ic6", 0x8000, 0x10001, 0xac883240},
};
const std::vector<RomEntry> kSubRom = {
    {"epr-6920.ic63", 0x8000, 0, 0x1c95013e},
    {"epr-6919.ic51", 0x8000, 1, 0x6ca30d69},
};
const std::vector<RomEntry> kSoundRom = {{"epr-6833.ic73", 0x4000, 0, 0x3b942f5f}};
const std::vector<RomEntry> kTileRom = {
    {"epr-6841.ic38", 0x8000, 0x0000, 0x54d295dc},
    {"epr-6842.ic23", 0x8000, 0x8000, 0xf677b568},
    {"epr-6843.ic7", 0x8000, 0x10000, 0xa257f0da},
};
const std::vector<RomEntry> kSpriteRom = {
    {"epr-6819.ic27", 0x8000, 0x00000, 0x469dad07},
    {"epr-6820.ic34", 0x8000, 0x00001, 0x87cbc6de},
    {"epr-6821.ic28", 0x8000, 0x10000, 0x15792969},
    {"epr-6822.ic35", 0x8000, 0x10001, 0xe9718de5},
    {"epr-6823.ic29", 0x8000, 0x20000, 0x49422691},
    {"epr-6824.ic36", 0x8000, 0x20001, 0x701deaa4},
    {"epr-6825.ic30", 0x8000, 0x30000, 0x6e23c8b4},
    {"epr-6826.ic37", 0x8000, 0x30001, 0x77d0de2c},
    {"epr-6827.ic31", 0x8000, 0x40000, 0x7fa1bfb6},
    {"epr-6828.ic38", 0x8000, 0x40001, 0x8e880c93},
    {"epr-6829.ic32", 0x8000, 0x50000, 0x7ca0952d},
    {"epr-6830.ic39", 0x8000, 0x50001, 0xb1a63aef},
    {"epr-6845.ic18", 0x8000, 0x60000, 0xba08c9b8},
    {"epr-6846.ic25", 0x8000, 0x60001, 0xf21e57a3},
};
const std::vector<RomEntry> kRoadRom = {{"epr-6840.ic108", 0x8000, 0, 0x581230e3}};
const std::vector<RomEntry> kPcmRom = {
    {"epr-6831.ic5", 0x8000, 0x0000, 0xcfef5481},
    {"epr-6832.ic6", 0x8000, 0x8000, 0x4165aea5},
};
const std::vector<RomEntry> kZoomRom = {{"epr-6844.ic123", 0x2000, 0, 0xe3ec7bd6}};

}  // namespace

HangOn::HangOn()
    : main_cpu_(kMainClock),
      sub_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(4000000, 0.3f, 0.3f),
      pcm_(8000000, 1.3f),
      framebuffer_(kScreenWidth * kScreenHeight, 0) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers([this](uint32_t a) { return sub_read(a); },
                                 [this](uint32_t a, uint16_t v) { sub_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [](uint16_t, uint8_t) {});
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });
    pcm_.set_read_rom([this](uint32_t addr) -> uint8_t {
        if (pcm_rom_.empty()) return 0x80;
        return pcm_rom_[addr % pcm_rom_.size()];
    });
    pcm_.set_bank(SegaPcm::kBank512);
    ppi0_.set_port_handlers(nullptr, nullptr, nullptr,
                            [this](uint8_t value) { sound_latch_ = value; },
                            [this](uint8_t value) {
                                z80_reset_ = (value & 0x20) == 0;
                                if (z80_reset_) sound_cpu_.reset();
                                video_.screen_enabled = (value & 0x10) != 0;
                            },
                            [this](uint8_t value) {
                                sound_cpu_.set_nmi((value & 0x80) ? IrqLine::Clear : IrqLine::Assert);
                            });
    ppi1_.set_port_handlers(nullptr, nullptr, nullptr,
                            [this](uint8_t value) {
                                sub_cpu_.set_irq(4, (value & 0x40) ? IrqLine::Clear : IrqLine::Assert);
                                sub_cpu_.set_reset_line((value & 0x20) ? IrqLine::Assert
                                                                       : IrqLine::Clear);
                                adc_select_ = (value >> 2) & 3;
                            },
                            nullptr, nullptr);
}

bool HangOn::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    video_.init_palette_luts();
    reset();
    return true;
}

bool HangOn::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    if (!load_roms16w(loader, kMainRom, rom_, error)) return false;
    if (!load_roms16w(loader, kSubRom, rom2_, error)) return false;
    std::vector<uint8_t> sound;
    if (!load_rom_bytes(loader, kSoundRom, sound, error)) return false;
    std::fill(sound_mem_.begin(), sound_mem_.end(), 0);
    std::copy(sound.begin(), sound.end(), sound_mem_.begin());
    std::vector<uint8_t> tiles;
    if (!load_rom_bytes(loader, kTileRom, tiles, error)) return false;
    decode_s16_tiles(video_.tiles, tiles, 1);
    if (!load_roms16b(loader, kSpriteRom, sprite_rom_, error)) return false;
    if (!load_rom_bytes(loader, kZoomRom, zoom_, error)) return false;
    std::vector<uint8_t> road;
    if (!load_rom_bytes(loader, kRoadRom, road, error)) return false;
    decode_hangon_road(road_gfx_, road);
    if (!load_rom_bytes(loader, kPcmRom, pcm_rom_, error)) return false;
    sprite_banks_ = 7;
    return true;
}

void HangOn::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    pcm_.reset();
    ppi0_.reset();
    ppi1_.reset();
    video_.reset();
    video_.screen_enabled = true;
    ram_.fill(0);
    ram2_.fill(0);
    road_ram_.fill(0);
    in0_ = 0xffff;
    adc_select_ = 0;
    sound_latch_ = 0;
    control_res_ = 0;
    z80_reset_ = false;
    analog_x_ = 0x80;
    analog_gas_ = 0;
    analog_brake_ = 0;
    audio_acc_ = 0;
    pcm_acc_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
}

void HangOn::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xffff;
    if (inputs.coin1) in0_ &= ~0x0001;
    if (inputs.coin2) in0_ &= ~0x0002;
    if (inputs.player1.start) {
        in0_ &= ~0x0010;
        in0_ &= ~0x0040;
    }
    analog_x_ = 0x80;
    if (inputs.player1.left) analog_x_ = 0x20;
    if (inputs.player1.right) analog_x_ = 0xe0;
    analog_gas_ = (inputs.player1.up || inputs.player1.button1) ? 0xff : 0;
    analog_brake_ = (inputs.player1.down || inputs.player1.button2) ? 0xff : 0;
}

void HangOn::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = uint16_t(0xff00 | value);
    if (bank == 1) dsw_b_ = uint16_t(0xff00 | value);
}

void HangOn::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

uint16_t HangOn::main_read(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x3ffff) {
        if (rom_.empty()) return 0xffff;
        return rom_[(address >> 1) % rom_.size()];
    }
    if (address >= 0x20c000 && address <= 0x20ffff) return ram_[(address & 0x3fff) >> 1];
    if (address >= 0x400000 && address <= 0x403fff) return video_.tile_ram[(address & 0x3fff) >> 1];
    if (address >= 0x410000 && address <= 0x410fff) return video_.char_ram[(address & 0xfff) >> 1];
    if (address >= 0x600000 && address <= 0x6007ff) return video_.sprite_ram[(address & 0x7ff) >> 1];
    if (address >= 0xa00000 && address <= 0xa00fff) return video_.pal_ram[(address & 0xfff) >> 1];
    if (address >= 0xc00000 && address <= 0xc3ffff) return rom2_[(address & 0x3ffff) >> 1];
    if (address >= 0xc68000 && address <= 0xc68fff) return road_ram_[(address & 0xfff) >> 1];
    if (address >= 0xc7c000 && address <= 0xc7ffff) return ram2_[(address & 0x3fff) >> 1];
    if (address >= 0xe00000 && address <= 0xe00fff) return ppi0_.read((address & 7) >> 1);
    if (address >= 0xe01000 && address <= 0xe01fff) {
        switch ((address & 7) >> 1) {
            case 0: return in0_;
            case 1: return dsw_a_;
            case 2: return dsw_b_;
            default: return 0xffff;
        }
    }
    if (address >= 0xe03000 && address <= 0xe03fff) {
        if ((address & 0x3f) <= 0x1f) return uint16_t(0xff00 | ppi1_.read((address & 7) >> 1));
        return uint16_t(0xff00 | control_res_);
    }
    return 0xffff;
}

void HangOn::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address >= 0x20c000 && address <= 0x20ffff) {
        ram_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x400000 && address <= 0x403fff) {
        const uint16_t offset = uint16_t((address & 0x3fff) >> 1);
        if (video_.tile_ram[offset] != value) {
            video_.tile_ram[offset] = value;
            video_.mark_tile(offset);
        }
        return;
    }
    if (address >= 0x410000 && address <= 0x410fff) {
        const uint16_t offset = uint16_t((address & 0xfff) >> 1);
        if (video_.char_ram[offset] != value) {
            video_.char_ram[offset] = value;
            video_.text_dirty[offset] = true;
        }
        video_.apply_screen_select_hangon(offset);
        return;
    }
    if (address >= 0x600000 && address <= 0x6007ff) {
        video_.sprite_ram[(address & 0x7ff) >> 1] = value;
        return;
    }
    if (address >= 0xa00000 && address <= 0xa00fff) {
        video_.set_palette_entry(int((address & 0xfff) >> 1), value, false);
        return;
    }
    if (address >= 0xc68000 && address <= 0xc68fff) {
        road_ram_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0xc7c000 && address <= 0xc7ffff) {
        ram2_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0xe00000 && address <= 0xe00fff) {
        ppi0_.write((address & 7) >> 1, uint8_t(value));
        return;
    }
    if (address >= 0xe03000 && address <= 0xe03fff) {
        if ((address & 0x3f) <= 0x1f) {
            ppi1_.write((address & 7) >> 1, uint8_t(value));
        } else {
            switch (adc_select_) {
                case 0: control_res_ = analog_x_; break;
                case 1: control_res_ = analog_gas_; break;
                case 2: control_res_ = analog_brake_; break;
                default: control_res_ = 0; break;
            }
        }
    }
}

uint16_t HangOn::sub_read(uint32_t address) {
    address &= 0x7ffff;
    if (address <= 0xffff) return rom2_[address >> 1];
    if (address >= 0x68000 && address <= 0x68fff) return road_ram_[(address & 0xfff) >> 1];
    if (address >= 0x7c000 && address <= 0x7ffff) return ram2_[(address & 0x3fff) >> 1];
    return 0xffff;
}

void HangOn::sub_write(uint32_t address, uint16_t value) {
    address &= 0x7ffff;
    if (address >= 0x68000 && address <= 0x68fff) road_ram_[(address & 0xfff) >> 1] = value;
    else if (address >= 0x7c000 && address <= 0x7ffff) ram2_[(address & 0x3fff) >> 1] = value;
}

uint8_t HangOn::sound_read(uint16_t address) {
    if (address <= 0x7fff) return sound_mem_[address];
    if (address >= 0xc000 && address <= 0xcfff) return sound_mem_[0xc000 + (address & 0x7ff)];
    if (address >= 0xd000 && address <= 0xdfff) return ym_.status();
    if (address >= 0xe000 && address <= 0xefff) return pcm_.read(address);
    return 0xff;
}

void HangOn::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0xc000 && address <= 0xcfff) {
        sound_mem_[0xc000 + (address & 0x7ff)] = value;
    } else if (address >= 0xd000 && address <= 0xdfff) {
        if ((address & 1) == 0) ym_.control(value);
        else ym_.write(value);
    } else if (address >= 0xe000 && address <= 0xefff) {
        pcm_.write(address, value);
    }
}

uint8_t HangOn::sound_in(uint16_t port) {
    if ((port & 0xff) >= 0x40 && (port & 0xff) <= 0x7f) return sound_latch_;
    return 0xff;
}

void HangOn::on_sound_cycles(int cycles) {
    pcm_acc_ += int64_t(cycles) * pcm_.tick_rate();
    while (pcm_acc_ >= kSoundClock) {
        pcm_acc_ -= kSoundClock;
        pcm_.clock();
    }
    audio_acc_ += int64_t(cycles) * YM2203::kSampleRate;
    while (audio_acc_ >= kSoundClock) {
        audio_acc_ -= kSoundClock;
        const int32_t sample = ym_.update() + pcm_.last_sample();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void HangOn::update_video() {
    if (!video_.screen_enabled) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), video_.palette[0x2000]);
        return;
    }
    video_.render_tile_pages(bg_, fg_, 0, true, 5, 0xfff, 0, false, false);
    std::vector<uint32_t> unused;
    video_.render_tile_pages(fg_, unused, 4, true, 5, 0xfff, 0, false, false);
    video_.render_text(text_low_, text_high_, 8, 0xff, 0x800, false);
    const int scroll_x1 = (0xc8 - (video_.char_ram[0x7fd] & 0x1ff)) & 0x3ff;
    const int scroll_y1 = video_.char_ram[0x793] & 0xff;
    const int scroll_x2 = (0xc8 - (video_.char_ram[0x7fc] & 0x1ff)) & 0x3ff;
    const int scroll_y2 = video_.char_ram[0x792] & 0xff;
    draw_hangon_road(framebuffer_.data(), video_.palette.data(), road_ram_.data(), road_gfx_.data(),
                     0x38, 0x7c0, 0);
    video_.blit_scrolled(framebuffer_.data(), bg_, scroll_x1, scroll_y1, 1024, 512);
    video_.blit_scrolled(framebuffer_.data(), fg_, scroll_x2, scroll_y2, 1024, 512);
    draw_hangon_road(framebuffer_.data(), video_.palette.data(), road_ram_.data(), road_gfx_.data(),
                     0x38, 0x7c0, 1);
    video_.blit_text(framebuffer_.data(), text_low_);
    for (int pri = 3; pri >= 0; pri--) {
        draw_sprites_hangon(video_, framebuffer_.data(), sprite_rom_, zoom_, sprite_banks_, pri);
    }
    video_.blit_text(framebuffer_.data(), text_high_);
}

void HangOn::run_frame() {
    const int main_cycles =
        int(double(kMainClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sound_cycles =
        int(double(kSoundClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 224) {
            main_cpu_.set_irq(4, IrqLine::Hold);
            update_video();
        }
        for (int slice = 0; slice < kCpuSync; slice++) {
            main_cpu_.run(main_cycles);
            sub_cpu_.run(main_cycles);
            if (!z80_reset_) sound_cpu_.run(sound_cycles);
        }
    }
}

}  // namespace dsp
