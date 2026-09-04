#include "drivers/arcade/simpsons.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// ROM_LOAD32_WORD: pairs every 4 bytes
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

// ROM_LOAD64_WORD: word every 8 bytes
std::vector<uint8_t> load64b(RomLoader& loader, const std::vector<RomEntry>& entries,
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
            dst += 8;
        }
    }
    return dest;
}

}  // namespace

Simpsons::Simpsons()
    : pens_(size_t(kScreenWidth) * kScreenHeight, 0),
      framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {}

bool Simpsons::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_lines_handler([this](uint8_t v) { rom_bank1_ = uint8_t(v & 0x3f); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });
    sound_cpu_.set_irq_ack_callback([this]() {
        main_snd_irq_ = false;
        update_sound_irq();
    });

    ym2151_.set_irq_handler([this](bool state) {
        ym_irq_ = state;
        update_sound_irq();
    });

    reset();
    return true;
}

void Simpsons::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    // MAME asserts NMI at reset; clear after a few cycles so IRQs can proceed
    sound_cpu_.set_nmi(IrqLine::Assert);
    nmi_timer_ = 200;  // auto-clear if sound never writes fa00
    ym2151_.reset();
    if (k053260_) k053260_->reset();
    if (k052109_) k052109_->reset();
    if (k053246_) k053246_->reset();
    k053251_.reset();
    eeprom_.reset();
    bank0_bank_ = 0;
    bank2000_bank_ = 0;
    rom_bank1_ = 0;
    sound_bank_ = 0;
    firq_enabled_ = false;
    in0_ = in1_ = in2_ = 0xff;
    sprite_dma_timer_ = 0;
    frame_main_ = 0;
    frame_snd_ = 0;
    sprite_ram_.fill(0);
    buffer_paleta_.fill(0);
}

void Simpsons::set_inputs(const MachineInputs& inputs) {
    auto bit = [](bool pressed, uint8_t& v, uint8_t mask) {
        if (pressed) v = uint8_t(v & ~mask);
        else v = uint8_t(v | mask);
    };
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    bit(p1.left, in0_, 0x01);
    bit(p1.right, in0_, 0x02);
    bit(p1.up, in0_, 0x04);
    bit(p1.down, in0_, 0x08);
    bit(p1.button1, in0_, 0x10);
    bit(p1.button2, in0_, 0x20);
    bit(p1.start, in0_, 0x80);

    bit(p2.left, in1_, 0x01);
    bit(p2.right, in1_, 0x02);
    bit(p2.up, in1_, 0x04);
    bit(p2.down, in1_, 0x08);
    bit(p2.button1, in1_, 0x10);
    bit(p2.button2, in1_, 0x20);
    bit(p2.start, in1_, 0x80);

    bit(inputs.coin1, in2_, 0x01);
    bit(inputs.coin2, in2_, 0x02);
}

void Simpsons::set_dip_switch(int, uint8_t) {}

void Simpsons::update_palette_entry(int index) {
    if (index < 0 || index >= 0x800) return;
    const uint16_t valor =
        uint16_t((buffer_paleta_[size_t(index * 2)] << 8) | buffer_paleta_[size_t(index * 2 + 1)]);
    const uint8_t r = uint8_t(((valor >> 0) & 0x1f) * 255 / 31);
    const uint8_t g = uint8_t(((valor >> 5) & 0x1f) * 255 / 31);
    const uint8_t b = uint8_t(((valor >> 10) & 0x1f) * 255 / 31);
    palette_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void Simpsons::objdma() {
    if (!k053246_) return;
    uint16_t* dst = k053246_->ram();
    int inac = 256;
    int count = 256;
    const uint16_t* src = sprite_ram_.data();
    do {
        if ((src[0] & 0x8000) != 0 && (src[0] & 0xff) != 0) {
            std::memcpy(dst, src, 16);
            dst += 8;
            inac--;
        }
        src += 8;
        count--;
    } while (count > 0);
    while (inac > 0) {
        dst[0] = 0;
        dst += 8;
        inac--;
    }
}

void Simpsons::run_frame() {
    const double main_c = double(kMainClock) / kFramesPerSecond / double(kScanlines);
    const double snd_c = double(kSoundClock) / kFramesPerSecond / double(kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 224) {
            if (k052109_ && k052109_->is_irq_enabled()) {
                main_cpu_.set_irq(IrqLine::Hold);
            }
            // Always DMA sprites at vblank (hardware does this when OBJCHA irq enabled;
            // also run when disabled so we still see list if game writes sprite_ram).
            if (k053246_) {
                objdma();
                if (k053246_->is_irq_enabled()) {
                    sprite_dma_timer_ = 1;
                }
            }
            update_video();
        }

        if (sprite_dma_timer_ > 0) {
            if (sprite_dma_timer_ == 1 && firq_enabled_) {
                main_cpu_.set_firq(IrqLine::Assert);
            }
            if (sprite_dma_timer_ == 8) {
                main_cpu_.set_firq(IrqLine::Clear);
                sprite_dma_timer_ = 0;
            } else if (sprite_dma_timer_ > 0) {
                sprite_dma_timer_++;
            }
        }

        main_cpu_.run(int(main_c + frame_main_));
        frame_main_ = (main_c + frame_main_) - int(main_c + frame_main_);

        sound_cpu_.run(int(snd_c + frame_snd_));
        if (nmi_timer_ > 0) {
            nmi_timer_ -= int(snd_c);
            if (nmi_timer_ <= 0) {
                nmi_timer_ = 0;
                sound_cpu_.set_nmi(IrqLine::Clear);
            }
        }
        frame_snd_ = (snd_c + frame_snd_) - int(snd_c + frame_snd_);
    }

    // Audio for one frame
    const int samples = int(double(YM2151::kSampleRate) / kFramesPerSecond + 0.5);
    std::vector<int16_t> k_l(size_t(samples), 0), k_r(size_t(samples), 0);
    if (k053260_) k053260_->update(samples, k_l.data(), k_r.data());
    audio_buffer_.resize(size_t(samples));
    for (int i = 0; i < samples; i++) {
        const int32_t s = ym2151_.update();
        audio_buffer_[size_t(i)] =
            int16_t(std::clamp(int(s) + (int(k_l[size_t(i)]) + int(k_r[size_t(i)])) / 2, -32768, 32767));
    }
}

void Simpsons::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_buffer_.begin(), audio_buffer_.end());
    audio_buffer_.clear();
}

void Simpsons::update_sound_irq() {
    // Combined IRQ: main command OR YM2151 timer
    const bool any = ym_irq_ || main_snd_irq_;
    sound_cpu_.set_irq(any ? IrqLine::Hold : IrqLine::Clear);
}

void Simpsons::on_sound_cycles(int cycles) {
    if (cycles > 0) ym2151_.run_timers(cycles);
}
void Simpsons::mix_audio_line(int) {}

void Simpsons::update_video() {
    if (!k052109_ || !k053246_) return;

    const uint8_t bg_colorbase = k053251_.get_palette_index(K053251::CI0);
    sprite_colorbase_ = k053251_.get_palette_index(K053251::CI1);
    const uint8_t lc0 = k053251_.get_palette_index(K053251::CI2);
    const uint8_t lc1 = k053251_.get_palette_index(K053251::CI3);
    const uint8_t lc2 = k053251_.get_palette_index(K053251::CI4);
    // Pascal: when K053251 palette index changes, dirty the tile layer
    if (lc0 != layer_colorbase_[0] || lc1 != layer_colorbase_[1] || lc2 != layer_colorbase_[2]) {
        k052109_->clean_video_buffer();
    }
    layer_colorbase_[0] = lc0;
    layer_colorbase_[1] = lc1;
    layer_colorbase_[2] = lc2;

    uint8_t sorted_layer[3] = {0, 1, 2};
    layerpri_[0] = k053251_.get_priority(K053251::CI2);
    layerpri_[1] = k053251_.get_priority(K053251::CI3);
    layerpri_[2] = k053251_.get_priority(K053251::CI4);
    K053251::sort_layers3(sorted_layer, layerpri_.data());

    constexpr int kNativeW = 288;
    constexpr int kNativeH = 224;
    constexpr int kCropX = 112;
    constexpr int kCropY = 16;
    const int npix = kNativeW * kNativeH;
    pens_.assign(size_t(npix), uint16_t(bg_colorbase * 16));

    std::vector<uint16_t> layer(size_t(npix), 0);
    // Ensure tile pens match current colorbase/bank every frame
    k052109_->clean_video_buffer();
    k052109_->draw_tiles();
    {
        static int fcnt;
        fcnt++;
        if (fcnt == 600) {
            // Count non-zero pens per layer after drawing to temp
            std::fprintf(stderr, "F600 lc=[%d,%d,%d] scb=%d bg=%d pri=[%d,%d,%d]\n",
                int(layer_colorbase_[0]), int(layer_colorbase_[1]), int(layer_colorbase_[2]),
                int(sprite_colorbase_), int(bg_colorbase),
                int(layerpri_[0]), int(layerpri_[1]), int(layerpri_[2]));
            std::fprintf(stderr, "F600 sorted=%d,%d,%d\n",
                int(sorted_layer[0]), int(sorted_layer[1]), int(sorted_layer[2]));
        }
    }

    k053246_->update_sprites();
    {
        static int fc; fc++;
        if (fc == 600 && k053246_) {
            const uint16_t* r = k053246_->ram();
            int n=0;
            for (int i=0;i<256;i++) if ((r[i*8]&0x8000) && (r[i*8]&0xff)) n++;
            std::fprintf(stderr, "SPR600 active=%d scb=%d\n", n, int(sprite_colorbase_));
            int shown=0;
            for (int i=0;i<256 && shown<12;i++) {
                if (!((r[i*8]&0x8000) && (r[i*8]&0xff))) continue;
                std::fprintf(stderr, "  [%d] %04x code=%04x y=%04x x=%04x\n",
                    i, r[i*8], r[i*8+1], r[i*8+2], r[i*8+3]);
                shown++;
            }
        }
    }

    // Sprites prio 3 (behind everything)
    k053246_->draw_sprites(pens_.data(), kNativeW, kNativeH, kCropX, kCropY, 3);

    for (int pass = 0; pass < 3; pass++) {
        std::fill(layer.begin(), layer.end(), 0);
        k052109_->draw_layer(sorted_layer[pass], layer.data(), kNativeW, kNativeH, kCropX, kCropY);
        for (int i = 0; i < npix; i++) {
            if (layer[size_t(i)]) pens_[size_t(i)] = layer[size_t(i)];
        }
        // Sprites between layers: prio 2,1,0
        k053246_->draw_sprites(pens_.data(), kNativeW, kNativeH, kCropX, kCropY,
                               uint8_t(2 - pass));
    }

    for (int y = 0; y < kNativeH; y++) {
        for (int x = 0; x < kNativeW; x++) {
            const uint16_t pen = pens_[size_t(y * kNativeW + x)] & 0x7ff;
            framebuffer_[size_t(y * kNativeW + x)] = palette_[size_t(pen)];
        }
    }
}

uint8_t Simpsons::main_read(uint16_t address) {
    if (address <= 0x0fff) {
        if (bank0_bank_ == 1) return buffer_paleta_[address];
        return k052109_ ? k052109_->read(address) : 0;
    }
    if (address >= 0x1000 && address <= 0x1fff) {
        switch (address) {
            case 0x1f80: return in2_;
            case 0x1f81:
                return uint8_t(0xcf | (eeprom_.do_read() << 4) | (eeprom_.ready_read() << 5));
            case 0x1f90: return in0_;
            case 0x1f91: return in1_;
            case 0x1f92:
            case 0x1f93: return 0xff;
            case 0x1fc4: {
                main_snd_irq_ = true;
                update_sound_irq();
                return 0;
            }
            case 0x1fc6:
            case 0x1fc7:
                return k053260_ ? k053260_->main_read(uint8_t(address & 1)) : 0;
            case 0x1fc8:
            case 0x1fc9:
                return k053246_ ? k053246_->read(uint8_t(address & 1)) : 0;
            case 0x1fca: return 0;  // watchdog
            default:
                return k052109_ ? k052109_->read(address) : 0;
        }
    }
    if (address >= 0x2000 && address <= 0x3fff) {
        if (bank2000_bank_ == 0) return k052109_ ? k052109_->read(address) : 0;
        if (address > 0x2fff) return memoria_[address];
        // sprite RAM
        const uint16_t tempw = uint16_t((address & 0xfff) >> 1);
        if (address & 1) return uint8_t(sprite_ram_[tempw] & 0xff);
        return uint8_t(sprite_ram_[tempw] >> 8);
    }
    if ((address >= 0x4000 && address <= 0x5fff) || address >= 0x8000) {
        return memoria_[address];
    }
    if (address >= 0x6000 && address <= 0x7fff) {
        return rom_bank_[rom_bank1_ & 0x3f][address & 0x1fff];
    }
    return 0;
}

void Simpsons::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        if (bank0_bank_ == 1) {
            if (buffer_paleta_[address] != value) {
                buffer_paleta_[address] = value;
                update_palette_entry(int(address >> 1));
                if (k052109_) k052109_->clean_video_buffer();
            }
        } else if (k052109_) {
            k052109_->write(address, value);
        }
        return;
    }
    if (address >= 0x1000 && address <= 0x1fff) {
        if (address >= 0x1fa0 && address <= 0x1fa7) {
            if (k053246_) k053246_->write(uint8_t(address & 7), value);
            return;
        }
        if (address >= 0x1fb0 && address <= 0x1fbf) {
            k053251_.write(uint8_t(address & 0xf), value);
            return;
        }
        if (address == 0x1fc0) {
            if (k052109_) k052109_->set_rmrd_line((value & 8) != 0);
            if (k053246_) k053246_->set_objcha_line((value & 0x20) == 0);
            return;
        }
        if (address == 0x1fc2) {
            if (value != 0xff) {
                eeprom_.di_write((value >> 7) & 1);
                eeprom_.cs_write((value >> 3) & 1);
                eeprom_.clk_write((value >> 4) & 1);
                bank0_bank_ = value & 1;
                bank2000_bank_ = (value >> 1) & 1;
                firq_enabled_ = (value & 4) != 0;
            }
            return;
        }
        if (address == 0x1fc6 || address == 0x1fc7) {
            if (k053260_) k053260_->main_write(uint8_t(address & 1), value);
            return;
        }
        if (k052109_) k052109_->write(address, value);
        return;
    }
    if (address >= 0x2000 && address <= 0x3fff) {
        if (bank2000_bank_ == 0) {
            if (k052109_) k052109_->write(address, value);
        } else if (address > 0x2fff) {
            memoria_[address] = value;
        } else {
            const uint16_t tempw = uint16_t((address & 0xfff) >> 1);
            if (address & 1)
                sprite_ram_[tempw] = uint16_t((sprite_ram_[tempw] & 0xff00) | value);
            else
                sprite_ram_[tempw] = uint16_t((sprite_ram_[tempw] & 0x00ff) | (uint16_t(value) << 8));
        }
        return;
    }
    if (address >= 0x4000 && address <= 0x5fff) {
        memoria_[address] = value;
    }
    // 6000-ffff ROM
}

uint8_t Simpsons::sound_read(uint16_t address) {
    if (address <= 0x7fff || (address >= 0xf000 && address <= 0xf7ff)) return mem_snd_[address];
    if (address >= 0x8000 && address <= 0xbfff)
        return sound_rom_bank_[sound_bank_ & 7][address & 0x3fff];
    if (address == 0xf801) {
        // Ensure timer B flag visible; advance timers more aggressively
        ym2151_.run_timers(1024);
        return ym2151_.status();
    }
    if (address >= 0xfc00 && address <= 0xfc2f)
        return k053260_ ? k053260_->read(uint8_t(address & 0x3f)) : 0;
    return 0;
}

void Simpsons::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;  // ROM
    if (address >= 0xf000 && address <= 0xf7ff) {
        mem_snd_[address] = value;
        return;
    }
    if (address == 0xf800) {
        ym2151_.select_register(value);
        return;
    }
    if (address == 0xf801) {
        ym2151_.write(value);
        return;
    }
    if (address == 0xfa00) {
        // MAME: clear NMI on fa00 write; K053260 timer later re-asserts
        // Pascal: assert then timer clears. Use clear so IRQ can run.
        sound_cpu_.set_nmi(IrqLine::Clear);
        nmi_timer_ = 0;
        return;
    }
    if (address >= 0xfc00 && address <= 0xfc2f) {
        if (k053260_) k053260_->write(uint8_t(address & 0x3f), value);
        return;
    }
    if (address == 0xfe00) sound_bank_ = value & 7;
}

bool Simpsons::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // Main program — Pascal order (2P set names with fallbacks)
    static const std::vector<RomEntry> kMain = {
        {"072-g02.16c", 0x20000, 0x00000, 0x580ce1d6},
        {"072-g01.17c", 0x20000, 0x20000, 0},  // may be g01 or p01
        {"072-j13.13c", 0x20000, 0x40000, 0},
        {"072-j12.15c", 0x20000, 0x60000, 0},
    };
    // Try alternate names for bank ROMs
    std::vector<uint8_t> main_rom(0x80000, 0);
    {
        std::vector<RomEntry> entries = kMain;
        // Prefer files present in zip
        auto names = loader.filenames();
        auto has = [&](const char* n) {
            for (const auto& s : names)
                if (s == n) return true;
            return false;
        };
        if (!has("072-g01.17c") && has("072-p01.17c")) entries[1].name = "072-p01.17c";
        if (!has("072-j13.13c") && has("072-013.13c")) entries[2].name = "072-013.13c";
        if (!has("072-j12.15c") && has("072-012.15c")) entries[3].name = "072-012.15c";
        if (!loader.load(entries, main_rom, error)) {
            // Try 4P set
            static const std::vector<RomEntry> kMain4p = {
                {"072-g02.16c", 0x20000, 0x00000, 0},
                {"072-g01.17c", 0x20000, 0x20000, 0},
                {"072-j13.13c", 0x20000, 0x40000, 0},
                {"072-j12.15c", 0x20000, 0x60000, 0},
            };
            if (!loader.load(kMain4p, main_rom, error)) return false;
        }
    }
    // Fixed bank at $8000 = last 8K of last 32K of region ($78000)
    std::memcpy(memoria_.data() + 0x8000, main_rom.data() + 0x78000, 0x8000);
    for (int f = 0; f < 0x40; f++) {
        std::memcpy(rom_bank_[size_t(f)].data(), main_rom.data() + f * 0x2000, 0x2000);
    }

    // Sound ROM
    static const std::vector<RomEntry> kSound = {
        {"072-g03.6g", 0x20000, 0, 0x76c1850c},
        {"072-e03.6g", 0x20000, 0, 0},  // alt
    };
    std::vector<uint8_t> snd(0x20000, 0);
    {
        std::vector<RomEntry> one = {kSound[0]};
        if (!loader.load(one, snd, error)) {
            one[0] = kSound[1];
            if (!loader.load(one, snd, error)) return false;
        }
    }
    // MAME layout: ROM_LOAD 0x8000 @0, CONTINUE 0x18000 @0x10000
    // Fixed Z80 $0000-$7FFF = first 8KB of file
    // Banks 0,1,2 = file+0x8000; banks 3..7 = file+0x8000+(n-2)*0x4000
    std::memcpy(mem_snd_.data(), snd.data(), 0x8000);
    for (int f = 0; f < 8; f++) {
        int off;
        if (f < 2)
            off = 0x8000;  // banks 0 and 1 same as bank 2 start
        else
            off = 0x8000 + (f - 2) * 0x4000;
        if (off + 0x4000 > int(snd.size())) off = int(snd.size()) - 0x4000;
        if (off < 0) off = 0;
        std::memcpy(sound_rom_bank_[size_t(f)].data(), snd.data() + off, 0x4000);
    }

    // Tiles
    static const std::vector<RomEntry> kTiles = {
        {"072-b07.18h", 0x80000, 0, 0xba1ec910},
        {"072-b06.16h", 0x80000, 2, 0xcf2bbcab},
    };
    std::vector<uint8_t> tiles = load32b(loader, kTiles, 0x100000, error);
    if (tiles.empty()) return false;

    // Sprites
    static const std::vector<RomEntry> kSprites = {
        {"072-b08.3n", 0x100000, 0, 0x7de500ad},
        {"072-b09.8n", 0x100000, 2, 0xaa085093},
        {"072-b10.12n", 0x100000, 4, 0x577dbd53},
        {"072-b11.16l", 0x100000, 6, 0x55fab05d},
    };
    std::vector<uint8_t> sprites = load64b(loader, kSprites, 0x400000, error);
    if (sprites.empty()) return false;

    // K053260 samples
    static const std::vector<RomEntry> kSamples = {
        {"072-d05.1f", 0x100000, 0, 0x1397a73b},
        {"072-d04.1d", 0x40000, 0x100000, 0x78778013},
    };
    std::vector<uint8_t> samples(0x140000, 0);
    if (!loader.load(kSamples, samples, error)) return false;

    // EEPROM default
    static const std::vector<RomEntry> kEeprom = {
        {"simpsons.12c.nv", 0x80, 0, 0},
        {"simpsons2p.12c.nv", 0x80, 0, 0xfbac4e30},
    };
    {
        std::vector<uint8_t> ee(0x80, 0xff);
        std::vector<RomEntry> one = {kEeprom[0]};
        if (!loader.load(one, ee, error)) {
            one[0] = kEeprom[1];
            loader.load(one, ee, error);  // optional
        }
        eeprom_.load(ee.data(), int(ee.size()));
    }

    k052109_ = std::make_unique<K052109>(
        [this](int layer, int bank, uint32_t& code, uint16_t& color, uint16_t& flags,
               uint16_t& priority) {
            code = code | (((color & 0x3f) << 8) | (uint32_t(bank) << 14));
            color = uint16_t(layer_colorbase_[size_t(layer % 3)] + ((color & 0xc0) >> 6));
            (void)flags;
            (void)priority;
        },
        std::move(tiles));

    k053246_ = std::make_unique<K053246>(
        [this](uint32_t& code, uint16_t& color, uint16_t& pri_mask) {
            const int pri = (color & 0xf80) >> 6;
            if (pri <= layerpri_[2])
                pri_mask = 0;
            else if (pri > layerpri_[2] && pri <= layerpri_[1])
                pri_mask = 1;
            else if (pri > layerpri_[1] && pri <= layerpri_[0])
                pri_mask = 2;
            else
                pri_mask = 3;
            color = uint16_t(sprite_colorbase_ + (color & 0x1f));
            (void)code;
        },
        std::move(sprites));
    k053246_->start(0, 16);

    k053260_ = std::make_unique<K053260>(std::move(samples), kSoundClock);

    return true;
}

}  // namespace dsp
