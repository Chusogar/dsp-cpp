#include "drivers/arcade/ajax.h"

#include <vector>
#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRom = {
    {"770_m01.n11", 0x10000, 0x00000, 0x4a64e53a},
    {"770_l02.n12", 0x10000, 0x10000, 0xad7d592b},
};
const std::vector<RomEntry> kSubRom = {
    {"770_l05.i16", 0x08000, 0x00000, 0xed64fbb2},
    {"770_f04.g16", 0x10000, 0x08000, 0xe0e4ec9c},
};
const RomEntry kSoundRom = {"770_h03.f16", 0x8000, 0, 0x2ffd2afc};

const std::vector<RomEntry> kTiles = {
    {"770c13-a.f3", 0x10000, 0x00000, 0x4ef6fff2}, {"770c13-c.f4", 0x10000, 0x00001, 0x97ffbab6},
    {"770c12-a.f5", 0x10000, 0x00002, 0x6c0ade68}, {"770c12-c.f6", 0x10000, 0x00003, 0x61fc39cc},
    {"770c13-b.e3", 0x10000, 0x40000, 0x86fdd706}, {"770c13-d.e4", 0x10000, 0x40001, 0x7d7acb2d},
    {"770c12-b.e5", 0x10000, 0x40002, 0x5f221cc6}, {"770c12-d.e6", 0x10000, 0x40003, 0xf1edb2f4},
};
const std::vector<RomEntry> kSprites = {
    {"770c09-a.f8", 0x10000, 0x00000, 0x76690fb8}, {"770c09-e.f9", 0x10000, 0x00001, 0x17b482c9},
    {"770c08-a.f10", 0x10000, 0x00002, 0xefd29a56}, {"770c08-e.f11", 0x10000, 0x00003, 0x6d43afde},
    {"770c09-b.e8", 0x10000, 0x40000, 0xcd1709d1}, {"770c09-f.e9", 0x10000, 0x40001, 0xcba4b47e},
    {"770c08-b.e10", 0x10000, 0x40002, 0xf3374014}, {"770c08-f.e11", 0x10000, 0x40003, 0xf5ba59aa},
    {"770c09-c.d8", 0x10000, 0x80000, 0xbfd080b8}, {"770c09-g.d9", 0x10000, 0x80001, 0x77d58ea0},
    {"770c08-c.d10", 0x10000, 0x80002, 0x28e7088f}, {"770c08-g.d11", 0x10000, 0x80003, 0x17da8f6d},
    {"770c09-d.c8", 0x10000, 0xc0000, 0x6f955600}, {"770c09-h.c9", 0x10000, 0xc0001, 0x494a9090},
    {"770c08-d.c10", 0x10000, 0xc0002, 0x91591777}, {"770c08-h.c11", 0x10000, 0xc0003, 0xd97d4b15},
};
const std::vector<RomEntry> kZoom = {
    {"770c06.f4", 0x40000, 0x00000, 0xd0c592ee},
    {"770c07.h4", 0x40000, 0x40000, 0x0b399fb1},
};
const std::vector<RomEntry> kK007232_1 = {
    {"770c10-a.a7", 0x10000, 0x00000, 0xe45ec094}, {"770c10-b.a6", 0x10000, 0x10000, 0x349db7d3},
    {"770c10-c.a5", 0x10000, 0x20000, 0x71cb1f05}, {"770c10-d.a4", 0x10000, 0x30000, 0xe8ab1844},
};
const std::vector<RomEntry> kK007232_2 = {
    {"770c11-a.c6", 0x10000, 0x00000, 0x8cccd9e0}, {"770c11-b.c5", 0x10000, 0x10000, 0x0af2fedd},
    {"770c11-c.c4", 0x10000, 0x20000, 0x7471f24a}, {"770c11-d.c3", 0x10000, 0x30000, 0xa58be323},
    {"770c11-e.b7", 0x10000, 0x40000, 0xdd553541}, {"770c11-f.b6", 0x10000, 0x50000, 0x3f78bd0f},
    {"770c11-g.b5", 0x10000, 0x60000, 0x078c51b2}, {"770c11-h.b4", 0x10000, 0x70000, 0x7300c2e1},
};

uint8_t pal5bit(uint16_t value) {
    value &= 0x1f;
    return uint8_t((value << 3) | (value >> 2));
}

// Interleaved 32-bit load used by ajax_tiles/sprites (roms_load32b_b).
std::vector<uint8_t> load32b_b(RomLoader& loader, const std::vector<RomEntry>& entries,
                               uint32_t total_size, std::string* error) {
    std::vector<uint8_t> dest(total_size, 0);
    for (const RomEntry& e : entries) {
        std::vector<uint8_t> part(e.length, 0);
        std::vector<RomEntry> one = {e};
        one[0].offset = 0;
        if (!loader.load(one, part, error)) return {};
        // Pascal roms_load32b_b: each ROM is written every 4th byte starting at e.offset.
        for (uint32_t i = 0; i < e.length; i++) {
            const uint32_t dst = e.offset + i * 4;
            if (dst < dest.size()) dest[dst] = part[i];
        }
    }
    return dest;
}

}  // namespace

Ajax::Ajax()
    : pens_(size_t(kNativeWidth) * kNativeHeight, 0),
      framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {}

bool Ajax::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sub_read(a); },
        [this](uint16_t a, uint8_t v) { sub_write(a, v); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });

    reset();
    return true;
}

void Ajax::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym2151_.reset();
    if (k007232_0_) k007232_0_->reset();
    if (k007232_1_) k007232_1_->reset();
    if (k052109_) k052109_->reset();
    if (k051960_) k051960_->reset();
    if (k051316_) k051316_->reset();
    sound_latch_ = 0;
    rom_bank1_ = 0;
    rom_bank2_ = 0;
    sub_firq_enable_ = false;
    gun_rand_ = 0;
    watchdog_ = 0;
    priority_ = false;
    in0_ = in1_ = in2_ = 0xff;
    // Pascal: frame_main := konami_0.tframes; (cycles per scanline)
    frame_main_ = double(main_cpu_.clock()) / kFramesPerSecond / kScanlines;
    frame_sub_ = double(kSubClock) / kFramesPerSecond / kScanlines;
    frame_snd_ = double(kSoundClock) / kFramesPerSecond / kScanlines;
    audio_accumulator_ = 0;
    audio_.clear();
}

void Ajax::run_frame() {
    // Match ajax_hw.pas ajax_principal cycle accounting:
    //   konami_0.run(frame_main);
    //   frame_main := frame_main + tframes - contador;
    // Same for sub and sound. Residual over/under-run carries to the next line.
    const double main_t = double(main_cpu_.clock()) / kFramesPerSecond / kScanlines;
    const double sub_t = double(kSubClock) / kFramesPerSecond / kScanlines;
    const double snd_t = double(kSoundClock) / kFramesPerSecond / kScanlines;

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            // MAME k052109 vblank IRQ → sub 6809 (M6809_IRQ_LINE).
            if (k052109_ && k052109_->is_irq_enabled()) {
                sub_cpu_.set_irq(IrqLine::Hold);
            }
            update_video();
        }

        // Budget for this line = residual from previous over/under-run.
        // Guard against non-positive after heavy overrun (run at least 1).
        auto budget = [](double residual, double tframes) {
            const int b = int(residual);
            return b > 0 ? b : (int(tframes) > 0 ? int(tframes) : 1);
        };
        const int main_used = main_cpu_.run(budget(frame_main_, main_t));
        frame_main_ = frame_main_ + main_t - double(main_used);

        const int sub_used = sub_cpu_.run(budget(frame_sub_, sub_t));
        frame_sub_ = frame_sub_ + sub_t - double(sub_used);

        const int snd_used = sound_cpu_.run(budget(frame_snd_, snd_t));
        frame_snd_ = frame_snd_ + snd_t - double(snd_used);

        if (k051960_) k051960_->update_line(line);
    }
    // Empty object-list walker at $6025 (bank 7): when $5900 head is 0 the
    // LDU/LDA loop spins forever. Real PCB hits the watchdog; Pascal never
    // hits this because timing keeps the list alive. Prefer a light RTS so
    // attract/logo state is preserved instead of a full CPU reset.
    if (main_cpu_.pc() == 0x6025) {
        if (++watchdog_ > 30) {
            watchdog_ = 0;
            // Simulate RTS: pop return address from S (big-endian)
            const uint16_t s = main_cpu_.s;
            const uint16_t ret = uint16_t((uint16_t(main_read(s)) << 8) | main_read(uint16_t(s + 1)));
            main_cpu_.s = uint16_t(s + 2);
            main_cpu_.set_pc(ret);
        }
    } else {
        watchdog_ = 0;
    }
}

void Ajax::set_inputs(const MachineInputs& inputs) {
    in0_ = in1_ = in2_ = 0xff;
    auto clear = [](uint8_t& p, int bit, bool on) {
        if (on) p = uint8_t(p & ~(1u << bit));
    };
    const InputState& p1 = inputs.player1;
    const InputState& p2 = inputs.player2;
    clear(in0_, 0, p1.left);
    clear(in0_, 1, p1.right);
    clear(in0_, 2, p1.up);
    clear(in0_, 3, p1.down);
    clear(in0_, 4, p1.button1);
    clear(in0_, 5, p1.button2);
    clear(in0_, 6, p1.button3);
    clear(in1_, 0, p2.left);
    clear(in1_, 1, p2.right);
    clear(in1_, 2, p2.up);
    clear(in1_, 3, p2.down);
    clear(in1_, 4, p2.button1);
    clear(in1_, 5, p2.button2);
    clear(in1_, 6, p2.button3);
    clear(in2_, 0, inputs.coin1);
    clear(in2_, 1, inputs.coin2);
    clear(in2_, 3, p1.start);
    clear(in2_, 4, p2.start);
}

void Ajax::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
    if (bank == 2) dsw_c_ = value;
}

void Ajax::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void Ajax::on_sound_cycles(int cycles) {
    if (cycles > 0) ym2151_.run_timers(cycles);
    audio_accumulator_ += int64_t(cycles) * int64_t(YM2151::kSampleRate);
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        int32_t sample = ym2151_.update();
        if (k007232_0_) sample += k007232_0_->update();
        if (k007232_1_) sample += k007232_1_->update();
        // YM2151 internal scale is quiet vs 16-bit full scale; boost for arcade levels.
        sample *= 24;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

uint8_t Ajax::main_read(uint16_t address) {
    if (address <= 0x01c0) {
        switch ((address & 0x1c0) >> 6) {
            case 0: return uint8_t(gun_rand_++ * 17 + 0x5a);  // MAME: random/gun port
            case 4: return in1_;
            case 6:
                switch (address & 3) {
                    case 0: return in2_;
                    case 1: return in0_;
                    case 2: return dsw_a_;
                    case 3: return dsw_b_;
                }
                break;
            case 7: return dsw_c_;
            default: break;
        }
        return 0xff;
    }
    if (address >= 0x0800 && address <= 0x0807)
        return k051960_ ? k051960_->k051937_read(uint8_t(address & 7)) : 0;
    if (address >= 0x0c00 && address <= 0x0fff)
        return k051960_ ? k051960_->read(uint16_t(address - 0x0c00)) : 0;
    if (address >= 0x1000 && address <= 0x1fff) return palette_ram_[address & 0xfff];
    if (address >= 0x2000 && address <= 0x5fff) return memoria_[address];
    if (address >= 0x6000 && address <= 0x7fff) {
        return rom_bank_[rom_bank1_ & 0x0f][address & 0x1fff];
    }
    return memoria_[address];
}

void Ajax::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x01c0) {
        switch ((address & 0x1c0) >> 6) {
            case 0:
                if (address == 0) {
                    if (sub_firq_enable_) sub_cpu_.set_firq(IrqLine::Hold);
                } else {
                    // MAME: non-zero offset in this mirror is watchdog kick
                    watchdog_ = 0;
                }
                break;
            case 1: sound_cpu_.set_irq(IrqLine::Hold); break;
            case 2: sound_latch_ = value; break;
            case 3:
                rom_bank1_ = uint8_t(((~value) & 0x80) >> 5) + (value & 7);
                priority_ = (value & 0x08) != 0;
                break;
            default: break;
        }
        return;
    }
    if (address >= 0x0800 && address <= 0x0807) {
        if (k051960_) k051960_->k051937_write(uint8_t(address & 7), value);
        return;
    }
    if (address >= 0x0c00 && address <= 0x0fff) {
        if (k051960_) k051960_->write(uint16_t(address - 0x0c00), value);
        return;
    }
    if (address >= 0x1000 && address <= 0x1fff) {
        if (palette_ram_[address & 0xfff] != value) {
            palette_ram_[address & 0xfff] = value;
            update_palette_entry(int(address & 0xfff) >> 1);
        }
        return;
    }
    if (address >= 0x2000 && address <= 0x5fff) {
        memoria_[address] = value;
        return;
    }
}

uint8_t Ajax::sub_read(uint16_t address) {
    if (address <= 0x07ff) return k051316_ ? k051316_->read(address) : 0;
    if (address >= 0x1000 && address <= 0x17ff) {
        return k051316_ ? k051316_->rom_read(address & 0x7ff) : 0;
    }
    if (address >= 0x2000 && address <= 0x3fff) return memoria_[address];
    if (address >= 0x4000 && address <= 0x7fff) {
        return k052109_ ? k052109_->read(address & 0x3fff) : 0;
    }
    if (address >= 0x8000 && address <= 0x9fff) {
        return rom_sub_bank_[rom_bank2_ % 9][address & 0x1fff];
    }
    return mem_misc_[address];
}

void Ajax::sub_write(uint16_t address, uint8_t value) {
    if (address <= 0x07ff) {
        if (k051316_) k051316_->write(address, value);
        return;
    }
    if (address >= 0x0800 && address <= 0x080f) {
        if (k051316_) k051316_->control_w(uint8_t(address & 0x0f), value);
        return;
    }
    if (address == 0x1800) {
        // MAME ajax sub_bankswitch_w:
        // bit6 RMRD, bit5 RVO wraparound, bit4 FIRQST, bits0-3 bank
        if (k052109_) k052109_->set_rmrd_line((value & 0x40) != 0);
        if (k051316_) k051316_->set_wraparound((value & 0x20) != 0);
        sub_firq_enable_ = (value & 0x10) != 0;
        rom_bank2_ = value & 0x0f;
        return;
    }
    if (address >= 0x2000 && address <= 0x3fff) {
        memoria_[address] = value;
        return;
    }
    if (address >= 0x4000 && address <= 0x7fff) {
        if (k052109_) k052109_->write(address & 0x3fff, value);
        return;
    }
}

uint8_t Ajax::sound_read(uint16_t address) {
    if (address <= 0x87ff) return mem_snd_[address];
    if (address >= 0xa000 && address <= 0xa00d) {
        return k007232_0_ ? k007232_0_->read(uint8_t(address & 0x0f)) : 0;
    }
    if (address >= 0xb000 && address <= 0xb00d) {
        return k007232_1_ ? k007232_1_->read(uint8_t(address & 0x0f)) : 0;
    }
    if (address == 0xc001) return ym2151_.status();
    if (address == 0xe000) return sound_latch_;
    return 0;
}

void Ajax::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address <= 0x87ff) {
        mem_snd_[address] = value;
        return;
    }
    if (address == 0x9000) {
        if (k007232_0_) k007232_0_->set_bank((value >> 1) & 1, value & 1);
        if (k007232_1_) k007232_1_->set_bank((value >> 4) & 3, (value >> 2) & 3);
        return;
    }
    if (address >= 0xa000 && address <= 0xa00d) {
        if (k007232_0_) k007232_0_->write(uint8_t(address & 0x0f), value);
        return;
    }
    if (address >= 0xb000 && address <= 0xb00d) {
        if (k007232_1_) k007232_1_->write(uint8_t(address & 0x0f), value);
        return;
    }
    if (address == 0xb80c) {
        if (k007232_1_) {
            const uint8_t v = uint8_t((value & 0x0f) * (0x11 >> 1));
            k007232_1_->set_volume(0, v, v);
        }
        return;
    }
    if (address == 0xc000) {
        ym2151_.select_register(value);
        return;
    }
    if (address == 0xc001) {
        ym2151_.write(value);
        return;
    }
}

void Ajax::update_palette_entry(int index) {
    if (index < 0 || index >= 0x800) return;
    const uint16_t value =
        uint16_t((palette_ram_[size_t(index * 2)] << 8) | palette_ram_[size_t(index * 2 + 1)]);
    const uint8_t b = pal5bit(value >> 10);
    const uint8_t g = pal5bit(value >> 5);
    const uint8_t r = pal5bit(value);
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void Ajax::update_video() {
    if (!k052109_ || !k051960_ || !k051316_) return;

    k052109_->draw_tiles();
    k051960_->update_sprites();  // DMA latch like MAME

    // MAME 0.260 ajax: screen.set_raw(24 MHz/3, 528, 108, 412, 256, 16, 240).
    // Visarea is 304×224 with origin (108,16). Tiles, sprites and K051316 all
    // use that cliprect; dest[0,0] is visarea pixel (108,16). Pascal used 112.
    constexpr int kCropX = 108;
    constexpr int kCropY = 16;

    const int npix = kNativeWidth * kNativeHeight;
    pens_.assign(size_t(npix), 0);
    std::vector<uint8_t> pri(size_t(npix), 0);
    std::vector<uint16_t> layer(size_t(npix), 0);

    auto composite = [&](uint8_t layer_pri) {
        for (int i = 0; i < npix; i++) {
            if (layer[size_t(i)]) {
                pens_[size_t(i)] = layer[size_t(i)];
                pri[size_t(i)] = layer_pri;
            }
        }
    };

    // MAME screen_update(ajax):
    //   B (tilemap 2) priority 1
    //   if m_priority: zoom pri 4, A pri 2
    //   else:          A pri 2, zoom pri 4
    //   sprites with priority masks
    //   F (tilemap 0) on top
    std::fill(layer.begin(), layer.end(), 0);
    k052109_->draw_layer(2, layer.data(), kNativeWidth, kNativeHeight, kCropX, kCropY);
    composite(0x01);  // GFX_PMASK_1

    if (priority_) {
        std::fill(layer.begin(), layer.end(), 0);
        k051316_->draw(layer.data(), kNativeWidth, kNativeHeight, kCropX, kCropY);
        composite(0x04);
        std::fill(layer.begin(), layer.end(), 0);
        k052109_->draw_layer(1, layer.data(), kNativeWidth, kNativeHeight, kCropX, kCropY);
        composite(0x02);
    } else {
        std::fill(layer.begin(), layer.end(), 0);
        k052109_->draw_layer(1, layer.data(), kNativeWidth, kNativeHeight, kCropX, kCropY);
        composite(0x02);
        std::fill(layer.begin(), layer.end(), 0);
        k051316_->draw(layer.data(), kNativeWidth, kNativeHeight, kCropX, kCropY);
        composite(0x04);
    }

    k051960_->draw_sprites_masked(pens_.data(), pri.data(), kNativeWidth, kNativeHeight, kCropX,
                                  kCropY);
    // Layer F always on top (MAME draws with priority 0 after sprites)
    std::fill(layer.begin(), layer.end(), 0);
    k052109_->draw_layer(0, layer.data(), kNativeWidth, kNativeHeight, kCropX, kCropY);
    for (int i = 0; i < npix; i++) {
        if (layer[size_t(i)]) pens_[size_t(i)] = layer[size_t(i)];
    }

    // Rotate 90° CW: native 304×224 → 224×304
    for (int y = 0; y < kNativeHeight; y++) {
        for (int x = 0; x < kNativeWidth; x++) {
            const uint16_t pen = pens_[size_t(y * kNativeWidth + x)];
            const uint32_t color = palette_[size_t(pen & 0x7ff)];
            const int rx = kNativeHeight - 1 - y;
            const int ry = x;
            if (rx >= 0 && rx < kScreenWidth && ry >= 0 && ry < kScreenHeight) {
                framebuffer_[size_t(ry * kScreenWidth + rx)] = color;
            }
        }
    }
}

bool Ajax::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x20000, 0);
    if (!loader.load(kMainRom, main, error)) return false;
    std::memcpy(memoria_.data() + 0x8000, main.data() + 0x8000, 0x8000);
    for (int f = 0; f < 4; f++) {
        std::memcpy(rom_bank_[size_t(f)].data(), main.data() + f * 0x2000, 0x2000);
    }
    for (int f = 0; f < 8; f++) {
        std::memcpy(rom_bank_[size_t(4 + f)].data(), main.data() + 0x10000 + f * 0x2000, 0x2000);
    }

    std::vector<uint8_t> sub(0x18000, 0);
    if (!loader.load(kSubRom, sub, error)) return false;
    std::memcpy(mem_misc_.data() + 0xa000, sub.data() + 0x2000, 0x6000);
    std::memcpy(rom_sub_bank_[8].data(), sub.data(), 0x2000);
    for (int f = 0; f < 8; f++) {
        std::memcpy(rom_sub_bank_[size_t(f)].data(), sub.data() + 0x8000 + f * 0x2000, 0x2000);
    }

    std::vector<uint8_t> sound(0x8000, 0);
    if (!loader.load({kSoundRom}, sound, error)) return false;
    std::memcpy(mem_snd_.data(), sound.data(), 0x8000);

    std::vector<uint8_t> pcm1(0x40000, 0);
    if (!loader.load(kK007232_1, pcm1, error)) return false;
    k007232_0_ = std::make_unique<K007232>(
        kSoundClock, std::move(pcm1), 0.20f, [this](uint8_t v) {
            k007232_0_->set_volume(0, uint8_t((v >> 4) * 0x11), 0);
            k007232_0_->set_volume(1, 0, uint8_t((v & 0x0f) * 0x11));
        });

    std::vector<uint8_t> pcm2(0x80000, 0);
    if (!loader.load(kK007232_2, pcm2, error)) return false;
    k007232_1_ = std::make_unique<K007232>(
        kSoundClock, std::move(pcm2), 0.50f,
        [this](uint8_t v) {
            k007232_1_->set_volume(1, uint8_t((v & 0x0f) * (0x11 >> 1)),
                                   uint8_t((v >> 4) * (0x11 >> 1)));
        },
        true);

    std::vector<uint8_t> tiles = load32b_b(loader, kTiles, 0x80000, error);
    if (tiles.empty()) return false;
    k052109_ = std::make_unique<K052109>(
        [](int layer, int bank, uint32_t& code, uint16_t& color, uint16_t&, uint16_t&) {
            static const int layer_colorbase[3] = {1024 / 16, 0 / 16, 512 / 16};
            code = code | (((color & 0x0f) << 8) | (uint32_t(bank) << 12));
            color = uint16_t(layer_colorbase[layer] + ((color & 0xf0) >> 4));
        },
        std::move(tiles));

    std::vector<uint8_t> sprites = load32b_b(loader, kSprites, 0x100000, error);
    if (sprites.empty()) return false;
    k051960_ = std::make_unique<K051960>(
        [](uint16_t& code, uint16_t& color, uint16_t& pri, uint16_t&) {
            // MAME ajax_state::sprite_callback — priority as GFX_PMASK bits:
            //   bit4 (0x10) → over zoom (PMASK_4)
            //   bit6 (0x40) clear → over A (PMASK_2)
            //   bit5 (0x20) → over B (PMASK_1)
            pri = 0;
            if (color & 0x10) pri = uint16_t(pri | 0x04);
            if ((color & 0x40) == 0) pri = uint16_t(pri | 0x02);
            if (color & 0x20) pri = uint16_t(pri | 0x01);
            color = uint16_t(16 + (color & 0x0f));
            (void)code;
        },
        std::move(sprites));
    k051960_->set_irq_callback([this](bool state) {
        main_cpu_.set_irq(state ? IrqLine::Hold : IrqLine::Clear);
    });
    k051960_->set_nmi_callback([this](bool state) {
        main_cpu_.set_nmi(state ? IrqLine::Hold : IrqLine::Clear);
    });
    ym2151_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Hold : IrqLine::Clear);
    });

    std::vector<uint8_t> zoom(0x80000, 0);
    if (!loader.load(kZoom, zoom, error)) return false;
    k051316_ = std::make_unique<K051316>(
        [](uint16_t& code, uint16_t& color, uint16_t&) {
            code = uint16_t(code | ((color & 0x07) << 8));
            color = uint16_t(6 + ((color & 0x08) >> 3));
        },
        std::move(zoom), K051316::Bpp::Bpp7);

    for (const std::string& w : loader.warnings()) warnings_.push_back(w);
    return true;
}

}  // namespace dsp
