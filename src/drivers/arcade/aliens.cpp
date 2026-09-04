#include "drivers/arcade/aliens.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr uint8_t kLayerColorBase[3] = {0, 4, 8};

uint8_t pal5bit(uint16_t value) {
    value &= 0x1f;
    return uint8_t((value << 3) | (value >> 2));
}

// Pascal roms_load32b: pairs of bytes every 4 positions (ROM_LOAD32_WORD style).
// For offset 0: fills 0,1,4,5,8,9...  For offset 2: fills 2,3,6,7,10,11...
std::vector<uint8_t> load32b(RomLoader& loader, const std::vector<RomEntry>& entries,
                             uint32_t total_size, std::string* error) {
    std::vector<uint8_t> dest(total_size, 0);
    for (const RomEntry& e : entries) {
        std::vector<uint8_t> part(e.length, 0);
        std::vector<RomEntry> one = {e};
        one[0].offset = 0;
        if (!loader.load(one, part, error)) return {};
        uint32_t dst = e.offset;
        for (uint32_t h = 0; h < (e.length / 2); h++) {
            if (dst < dest.size()) dest[dst] = part[h * 2];
            if (dst + 1 < dest.size()) dest[dst + 1] = part[h * 2 + 1];
            dst += 4;
        }
    }
    return dest;
}

}  // namespace

Aliens::Aliens()
    : pens_(size_t(kScreenWidth) * kScreenHeight, 0),
      framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {}

bool Aliens::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_lines_handler([this](uint8_t v) { rom_bank1_ = uint8_t(v & 0x1f); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });

    ym2151_.set_port_handler([this](uint8_t v) {
        if (k007232_) k007232_->set_bank((v >> 1) & 1, v & 1);
    });
    ym2151_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Hold : IrqLine::Clear);
    });

    if (k051960_) {
        k051960_->set_irq_callback([this](bool state) {
            main_cpu_.set_irq(state ? IrqLine::Hold : IrqLine::Clear);
        });
    }

    reset();
    return true;
}

void Aliens::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym2151_.reset();
    if (k007232_) k007232_->reset();
    if (k052109_) k052109_->reset();
    if (k051960_) k051960_->reset();
    sound_latch_ = 0;
    bank0_bank_ = 0;
    rom_bank1_ = 0;
    rmrd_ = false;
    in0_ = in1_ = 0xff;
    audio_accumulator_ = 0;
    audio_.clear();
}

void Aliens::run_frame() {
    // Effective Konami-1 rate is clock/4 (matches cpu_konami.create(12MHz) in Pascal).
    const int main_c = int(double(main_cpu_.clock()) / kFramesPerSecond / kScanlines + 0.5);
    const int snd_c = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        // Assert VBlank IRQ at the start of line 240 so the main CPU can
        // service it during this line's cycles (timer DP:$1A ticks in $81F5).
        if (k051960_) k051960_->update_line(line);
        if (line == 240) update_video();
        main_cpu_.run(main_c);
        sound_cpu_.run(snd_c);
        mix_audio_line(snd_c);
    }
}

void Aliens::set_inputs(const MachineInputs& inputs) {
    in0_ = in1_ = 0xff;
    auto clear = [](uint8_t& p, int bit, bool on) {
        if (on) p = uint8_t(p & ~(1u << bit));
    };
    const InputState& p1 = inputs.player1;
    const InputState& p2 = inputs.player2;
    // P1: left,right,up,down,but0,but1,coin,start
    clear(in0_, 0, p1.left);
    clear(in0_, 1, p1.right);
    clear(in0_, 2, p1.up);
    clear(in0_, 3, p1.down);
    clear(in0_, 4, p1.button1);
    clear(in0_, 5, p1.button2);
    clear(in0_, 6, inputs.coin1);
    clear(in0_, 7, p1.start);
    // P2
    clear(in1_, 0, p2.left);
    clear(in1_, 1, p2.right);
    clear(in1_, 2, p2.up);
    clear(in1_, 3, p2.down);
    clear(in1_, 4, p2.button1);
    clear(in1_, 5, p2.button2);
    clear(in1_, 6, inputs.coin2);
    clear(in1_, 7, p2.start);
}

void Aliens::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
    if (bank == 2) dsw_c_ = value;
}

void Aliens::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void Aliens::on_sound_cycles(int cycles) {
    // Samples are produced in run_frame from a fixed clock; cycle_handler only
    // keeps YM2151 timers in sync with Z80 activity.
    if (cycles > 0) ym2151_.run_timers(cycles);
}

void Aliens::mix_audio_line(int sound_cycles) {
    audio_accumulator_ += int64_t(sound_cycles) * int64_t(YM2151::kSampleRate);
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        int32_t sample = ym2151_.update();
        if (k007232_) sample += k007232_->update();
        sample *= 24;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

uint8_t Aliens::main_read(uint16_t address) {
    if (address <= 0x03ff) {
        return ram_bank_[bank0_bank_][address];
    }
    if ((address >= 0x0400 && address <= 0x1fff) || address >= 0x8000) {
        return memoria_[address];
    }
    if (address >= 0x2000 && address <= 0x3fff) {
        return rom_bank_[size_t(rom_bank1_ % 24)][address & 0x1fff];
    }
    if (address >= 0x4000 && address <= 0x7fff) {
        switch (address) {
            case 0x5f80: return dsw_c_;
            case 0x5f81: return in0_;
            case 0x5f82: return in1_;
            case 0x5f83: return dsw_b_;
            case 0x5f84: return dsw_a_;
            default:
                break;
        }
        const uint16_t a = uint16_t(address & 0x3fff);
        if (!rmrd_) {
            if (a <= 0x37ff || (a >= 0x3808 && a <= 0x3bff)) {
                return k052109_ ? k052109_->read(a) : 0;
            }
            if (a >= 0x3800 && a <= 0x3807) {
                return k051960_ ? k051960_->k051937_read(uint8_t(a - 0x3800)) : 0;
            }
            if (a >= 0x3c00 && a <= 0x3fff) {
                return k051960_ ? k051960_->read(uint16_t(a - 0x3c00)) : 0;
            }
            return 0;
        }
        return k052109_ ? k052109_->read(a) : 0;
    }
    return 0;
}

void Aliens::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x03ff) {
        ram_bank_[bank0_bank_][address] = value;
        if (bank0_bank_ == 1) {
            if (palette_ram_[address] != value) {
                palette_ram_[address] = value;
                update_palette_entry(int(address) >> 1);
            }
        }
        return;
    }
    if (address >= 0x0400 && address <= 0x1fff) {
        memoria_[address] = value;
        return;
    }
    if (address >= 0x2000 && address <= 0x3fff) return;  // banked ROM
    if (address >= 0x4000 && address <= 0x7fff) {
        if (address == 0x5f88) {
            bank0_bank_ = uint8_t((value & 0x20) >> 5);
            rmrd_ = (value & 0x40) != 0;
            if (k052109_) k052109_->set_rmrd_line(rmrd_);
            return;
        }
        if (address == 0x5f8c) {
            sound_latch_ = value;
            sound_cpu_.set_irq(IrqLine::Hold);
            return;
        }
        const uint16_t a = uint16_t(address & 0x3fff);
        if (a <= 0x37ff || (a >= 0x3808 && a <= 0x3bff)) {
            if (k052109_) k052109_->write(a, value);
            return;
        }
        if (a >= 0x3800 && a <= 0x3807) {
            if (k051960_) k051960_->k051937_write(uint8_t(a - 0x3800), value);
            return;
        }
        if (a >= 0x3c00 && a <= 0x3fff) {
            if (k051960_) k051960_->write(uint16_t(a - 0x3c00), value);
            return;
        }
        return;
    }
    // $8000-$ffff ROM
}

uint8_t Aliens::sound_read(uint16_t address) {
    if (address <= 0x87ff) return mem_snd_[address];
    if (address == 0xa000 || address == 0xa001) return ym2151_.status();
    if (address == 0xc000) return sound_latch_;
    if (address >= 0xe000 && address <= 0xe00d) {
        return k007232_ ? k007232_->read(uint8_t(address & 0x0f)) : 0;
    }
    return 0;
}

void Aliens::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address <= 0x87ff) {
        mem_snd_[address] = value;
        return;
    }
    if (address == 0xa000) {
        ym2151_.select_register(value);
        return;
    }
    if (address == 0xa001) {
        ym2151_.write(value);
        return;
    }
    if (address >= 0xe000 && address <= 0xe00d) {
        if (k007232_) k007232_->write(uint8_t(address & 0x0f), value);
        return;
    }
}

void Aliens::update_palette_entry(int index) {
    if (index < 0 || index >= 0x200) return;
    const uint16_t value =
        uint16_t((palette_ram_[size_t(index * 2)] << 8) | palette_ram_[size_t(index * 2 + 1)]);
    const uint8_t b = pal5bit(value >> 10);
    const uint8_t g = pal5bit(value >> 5);
    const uint8_t r = pal5bit(value);
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void Aliens::update_video() {
    if (!k052109_ || !k051960_) return;

    k052109_->draw_tiles();
    k051960_->update_sprites();

    // fill with layer_colorbase[1]*16 = 64
    pens_.assign(size_t(kScreenWidth) * kScreenHeight, uint16_t(kLayerColorBase[1] * 16));

    const int crop_x = 112, crop_y = 16;
    k051960_->draw_sprites(7, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);
    k052109_->draw_layer(1, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);
    k051960_->draw_sprites(6, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);
    k052109_->draw_layer(2, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);
    k051960_->draw_sprites(4, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);
    k052109_->draw_layer(0, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);
    k051960_->draw_sprites(0, pens_.data(), kScreenWidth, kScreenHeight, crop_x, crop_y);

    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const uint16_t pen = pens_[size_t(y * kScreenWidth + x)];
            framebuffer_[size_t(y * kScreenWidth + x)] = palette_[size_t(pen & 0x1ff)];
        }
    }
}

bool Aliens::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // Main CPU ROMs → 0x30000 temp, then bank + fixed
    const std::vector<RomEntry> kMain = {
        {"875_j01.c24", 0x20000, 0x00000, 0x6a529cd6},
        {"875_j02.e24", 0x10000, 0x20000, 0x56c20971},
    };
    std::vector<uint8_t> main_rom(0x30000, 0);
    if (!loader.load(kMain, main_rom, error)) return false;
    std::memcpy(memoria_.data() + 0x8000, main_rom.data() + 0x28000, 0x8000);
    for (int f = 0; f < 24; f++) {
        std::memcpy(rom_bank_[size_t(f)].data(), main_rom.data() + f * 0x2000, 0x2000);
    }

    // Sound ROM
    const std::vector<RomEntry> kSound = {{"875_b03.g04", 0x8000, 0, 0x1ac4d283}};
    std::vector<uint8_t> sound(0x8000, 0);
    if (!loader.load(kSound, sound, error)) return false;
    std::memcpy(mem_snd_.data(), sound.data(), 0x8000);

    // K007232 PCM
    const std::vector<RomEntry> kPcm = {{"875b04.e05", 0x40000, 0, 0x4e209ac8}};
    std::vector<uint8_t> pcm(0x40000, 0);
    if (!loader.load(kPcm, pcm, error)) return false;
    k007232_ = std::make_unique<K007232>(
        kSoundClock, std::move(pcm), 0.20f, [this](uint8_t v) {
            if (!k007232_) return;
            k007232_->set_volume(0, uint8_t((v & 0x0f) * 0x11), 0);
            k007232_->set_volume(1, 0, uint8_t((v >> 4) * 0x11));
        });

    // Tiles (roms_load32b)
    const std::vector<RomEntry> kTiles = {
        {"875b11.k13", 0x80000, 0x000000, 0x89c5c885},
        {"875b12.k19", 0x80000, 0x000002, 0xea6bdc17},
        {"875b07.j13", 0x40000, 0x100000, 0xe9c56d66},
        {"875b08.j19", 0x40000, 0x100002, 0xf9387966},
    };
    std::vector<uint8_t> tiles = load32b(loader, kTiles, 0x200000, error);
    if (tiles.empty()) return false;
    k052109_ = std::make_unique<K052109>(
        [](int layer, int bank, uint32_t& code, uint16_t& color, uint16_t& /*flags*/,
           uint16_t& /*priority*/) {
            code = code | (((color & 0x3f) << 8) | (uint32_t(bank) << 14));
            color = uint16_t(kLayerColorBase[layer % 3] + ((color & 0xc0) >> 6));
        },
        std::move(tiles));

    // Sprites (roms_load32b, tipo=2)
    const std::vector<RomEntry> kSprites = {
        {"875b10.k08", 0x80000, 0x000000, 0x0b1035b1},
        {"875b09.k02", 0x80000, 0x000002, 0xe76b3c19},
        {"875b06.j08", 0x40000, 0x100000, 0x081a0566},
        {"875b05.j02", 0x40000, 0x100002, 0x19a261f2},
    };
    std::vector<uint8_t> sprites = load32b(loader, kSprites, 0x200000, error);
    if (sprites.empty()) return false;
    k051960_ = std::make_unique<K051960>(
        [](uint16_t& code, uint16_t& color, uint16_t& pri, uint16_t& shadow) {
            switch (color & 0x70) {
                case 0x20:
                case 0x60:
                    pri = 7;
                    break;
                case 0x00:
                    pri = 4;
                    break;
                case 0x40:
                    pri = 6;
                    break;
                case 0x10:
                    pri = 0;
                    break;
                case 0x50:
                    pri = 2;
                    break;
                case 0x30:
                case 0x70:
                    pri = 3;
                    break;
                default:
                    pri = 0;
                    break;
            }
            code = uint16_t(code | ((color & 0x80) << 6));
            color = uint16_t(16 + (color & 0x0f));
            shadow = 0;
        },
        std::move(sprites), 4);

    return true;
}

}  // namespace dsp
