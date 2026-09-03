#include "drivers/bublbobl.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

inline uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t(n * 0x11);
}

const std::vector<RomEntry> kMainRoms = {
    {"a78-06-1.51", 0x8000, 0x0000, 0x567934b6},
    {"a78-05-1.52", 0x10000, 0x8000, 0x9f8ee242},
};
const std::vector<RomEntry> kSubRoms = {{"a78-08.37", 0x8000, 0, 0xae11a07b}};
const std::vector<RomEntry> kSoundRoms = {{"a78-07.46", 0x8000, 0, 0x4f9a26e8}};
const std::vector<RomEntry> kMcuRoms = {{"a78-01.17", 0x1000, 0, 0xb1bfb53d}};
const std::vector<RomEntry> kPromRoms = {{"a71-25.41", 0x100, 0, 0x2d0f8545}};
const std::vector<RomEntry> kGfxRoms = {
    {"a78-09.12", 0x8000, 0x00000, 0x20358c22}, {"a78-10.13", 0x8000, 0x08000, 0x930168a9},
    {"a78-11.14", 0x8000, 0x10000, 0x9773e512}, {"a78-12.15", 0x8000, 0x18000, 0xd045549b},
    {"a78-13.16", 0x8000, 0x20000, 0xd0af35c5}, {"a78-14.17", 0x8000, 0x28000, 0x7b5369a8},
    {"a78-15.30", 0x8000, 0x40000, 0x6b61a413}, {"a78-16.31", 0x8000, 0x48000, 0xb5492d97},
    {"a78-17.32", 0x8000, 0x50000, 0xd69762d5}, {"a78-18.33", 0x8000, 0x58000, 0x9f243b68},
    {"a78-19.34", 0x8000, 0x60000, 0x66e9438c}, {"a78-20.35", 0x8000, 0x68000, 0x9ef863ad},
};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x4000;
    layout.planes = 4;
    layout.char_increment = 16 * 8;
    // $4000*16*8 bits = RGN_FRAC(1,2) of the 512K gfx ROM.
    const int plane2 = 0x4000 * 16 * 8;
    layout.plane_offsets = {0, 4, plane2, plane2 + 4};
    layout.x_offsets = {3, 2, 1, 0, 8 + 3, 8 + 2, 8 + 1, 8 + 0};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
    return layout;
}

}  // namespace

BublBobl::BublBobl()
    : bitmap_(256u * 256u, 0xff000000u),
      framebuffer_(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_irq_ack_callback([this]() { main_cpu_.set_irq(IrqLine::Clear, memory_[0xfc00]); });

    sub_cpu_.set_memory_handlers([this](uint16_t a) { return sub_read(a); },
                                 [this](uint16_t a, uint8_t v) { sub_write(a, v); });

    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });

    mcu_.set_port_read(0, [this]() { return in0_; });
    mcu_.set_port_read(2, [this]() { return mcu_port3_in_; });
    mcu_.set_port_write(0, [this](uint8_t v) { mcu_port1_write(v); });
    mcu_.set_port_write(1, [this](uint8_t v) { mcu_port2_write(v); });
    mcu_.set_port_write(2, [this](uint8_t v) { mcu_port3_out_ = v; });
    mcu_.set_port_write(3, [this](uint8_t v) { mcu_port4_out_ = v; });

    ym2203_.set_irq_handler([this](bool state) {
        ym2203_irq_ = state;
        update_sound_irq();
    });
    ym3526_.set_irq_handler([this](bool state) {
        ym3526_irq_ = state;
        update_sound_irq();
    });
}

bool BublBobl::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool BublBobl::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x18000, 0);
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.begin() + 0x8000, memory_.begin());
    for (int bank = 0; bank < 8; bank++) bank_rom_[size_t(bank)].fill(0xff);
    for (int bank = 0; bank < 4; bank++) {
        const uint8_t* src = main_rom.data() + 0x8000 + bank * 0x4000;
        std::copy(src, src + 0x4000, bank_rom_[size_t(bank)].begin());
    }

    std::vector<uint8_t> sub_rom(0x8000, 0);
    if (!loader.load(kSubRoms, sub_rom, error)) return false;
    sub_rom_.fill(0xff);
    std::copy(sub_rom.begin(), sub_rom.end(), sub_rom_.begin());

    std::vector<uint8_t> sound_rom(0x8000, 0);
    if (!loader.load(kSoundRoms, sound_rom, error)) return false;
    sound_mem_.fill(0);
    std::copy(sound_rom.begin(), sound_rom.end(), sound_mem_.begin());

    std::vector<uint8_t> mcu_rom(0x1000, 0);
    if (!loader.load(kMcuRoms, mcu_rom, error)) return false;
    mcu_.internal_rom().fill(0xff);
    // M6801U4 ROM lives at $F000-$FFFF; HD63701Y maps $C000-$FFFF as 16K, so
    // the 4K image sits in the last quarter.
    std::copy(mcu_rom.begin(), mcu_rom.end(), mcu_.internal_rom().begin() + 0x3000);

    std::vector<uint8_t> prom(0x100, 0);
    if (!loader.load(kPromRoms, prom, error)) return false;
    std::copy(prom.begin(), prom.end(), prom_.begin());

    std::vector<uint8_t> gfx_rom(0x80000, 0);
    if (!loader.load(kGfxRoms, gfx_rom, error)) return false;
    decode_graphics(gfx_rom);

    warnings_ = loader.warnings();
    return true;
}

void BublBobl::decode_graphics(const std::vector<uint8_t>& gfx_rom) {
    // convert_gfx(..., invert=true): each plane bit is flipped, so ROM 0
    // becomes pen 15 (transparent). Empty object columns then do not cover
    // the tiles that actually have pixels.
    std::vector<uint8_t> inverted = gfx_rom;
    for (uint8_t& value : inverted) value = uint8_t(~value);
    gfx_.decode(char_layout(), inverted);
}

void BublBobl::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    mcu_.reset();
    ym2203_.reset();
    ym3526_.reset();

    bank_ = 0;
    sound_stat_ = 0;
    sound_latch_ = 0;
    sound_nmi_ = false;
    video_enable_ = false;
    flip_screen_ = false;
    in0_ = 0xb3;
    in1_ = 0xff;
    in2_ = 0xff;
    dsw_a_ = 0xfe;
    dsw_b_ = 0xff;
    mcu_port1_out_ = 0;
    mcu_port2_out_ = 0;
    mcu_port3_in_ = 0;
    mcu_port3_out_ = 0;
    mcu_port4_out_ = 0;
    ym2203_irq_ = false;
    ym3526_irq_ = false;
    frame_main_ = 0;
    frame_sub_ = 0;
    frame_snd_ = 0;
    frame_mcu_ = 0;
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    audio_buffer_.clear();

    // Work RAM only; keep the loaded main ROM in $0000-$7FFF.
    std::memset(memory_.data() + 0xc000, 0, 0x4000);
    std::memset(sound_mem_.data() + 0x8000, 0, 0x8000);

    set_sub_reset(true);
    set_sound_reset(false);
    set_mcu_reset(true);
}

void BublBobl::set_sub_reset(bool held) {
    if (held && !sub_reset_) sub_cpu_.reset();
    sub_reset_ = held;
}

void BublBobl::set_sound_reset(bool held) {
    if (held && !sound_reset_) {
        sound_cpu_.reset();
        sound_cpu_.set_irq(IrqLine::Clear);
        sound_cpu_.set_nmi(IrqLine::Clear);
        sound_nmi_ = false;
    }
    sound_reset_ = held;
}

void BublBobl::set_mcu_reset(bool held) {
    mcu_held_ = held;
    mcu_.set_reset(held ? IrqLine::Assert : IrqLine::Clear);
}

void BublBobl::update_sound_irq() {
    sound_cpu_.set_irq((ym2203_irq_ || ym3526_irq_) ? IrqLine::Assert : IrqLine::Clear);
}

void BublBobl::set_inputs(const MachineInputs& inputs) {
    auto clear_bit = [](bool pressed, uint8_t& v, uint8_t mask) {
        if (pressed) v = uint8_t(v & ~mask);
        else v = uint8_t(v | mask);
    };

    in1_ = in2_ = 0xff;
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    clear_bit(p1.left, in1_, 0x01);
    clear_bit(p1.right, in1_, 0x02);
    clear_bit(p1.down, in1_, 0x04);
    clear_bit(p1.up, in1_, 0x08);
    clear_bit(p1.button2, in1_, 0x10);
    clear_bit(p1.button1, in1_, 0x20);
    clear_bit(p1.start, in1_, 0x40);

    // Pascal events_bublbobl copies P1 down/up onto P2; use player 2 here.
    clear_bit(p2.left, in2_, 0x01);
    clear_bit(p2.right, in2_, 0x02);
    clear_bit(p2.down, in2_, 0x04);
    clear_bit(p2.up, in2_, 0x08);
    clear_bit(p2.button2, in2_, 0x10);
    clear_bit(p2.button1, in2_, 0x20);
    clear_bit(p2.start, in2_, 0x40);

    // Coins are active-high on IN0 bits 2 and 3. Default $B3.
    in0_ = 0xb3;
    if (inputs.coin1) in0_ = uint8_t(in0_ | 0x04);
    if (inputs.coin2) in0_ = uint8_t(in0_ | 0x08);
}

void BublBobl::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void BublBobl::run_frame() {
    const double main_c = double(kMainClock) / kFramesPerSecond / double(kScanlines);
    const double sub_c = double(kSubClock) / kFramesPerSecond / double(kScanlines);
    const double snd_c = double(kSoundClock) / kFramesPerSecond / double(kScanlines);
    const double mcu_c = double(kMcuClock) / kFramesPerSecond / double(kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (!sub_reset_) sub_cpu_.set_irq(IrqLine::Hold);
            if (!mcu_held_) mcu_.set_irq(IrqLine::Hold);
            update_video();
        }

        main_cpu_.run(int(main_c + frame_main_));
        frame_main_ = (main_c + frame_main_) - int(main_c + frame_main_);

        if (!sub_reset_) {
            sub_cpu_.run(int(sub_c + frame_sub_));
            frame_sub_ = (sub_c + frame_sub_) - int(sub_c + frame_sub_);
        } else {
            frame_sub_ = 0;
        }

        if (!sound_reset_) {
            sound_cpu_.run(int(snd_c + frame_snd_));
            frame_snd_ = (snd_c + frame_snd_) - int(snd_c + frame_snd_);
        } else {
            frame_snd_ = 0;
        }

        mcu_.run(int(mcu_c + frame_mcu_));
        frame_mcu_ = (mcu_c + frame_mcu_) - int(mcu_c + frame_mcu_);
    }

    const int samples = int(double(YM2203::kSampleRate) / kFramesPerSecond + 0.5);
    audio_buffer_.resize(size_t(samples));
    for (int i = 0; i < samples; i++) {
        int32_t s = ym2203_.update() + ym3526_.update();
        s *= 6;
        audio_buffer_[size_t(i)] = int16_t(std::clamp(s, int32_t(-32768), int32_t(32767)));
    }
}

void BublBobl::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_buffer_.begin(), audio_buffer_.end());
    audio_buffer_.clear();
}

void BublBobl::set_color(int index) {
    if (index < 0 || index >= 0x100) return;
    const int even = index * 2;
    const uint8_t rg = palette_ram_[size_t(even)];
    const uint8_t b = palette_ram_[size_t(even + 1)];
    const uint8_t r = pal4bit(uint8_t(rg >> 4));
    const uint8_t g = pal4bit(rg);
    const uint8_t blue = pal4bit(uint8_t(b >> 4));
    palette_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | blue;
}

uint8_t BublBobl::main_read(uint16_t address) {
    if (address <= 0x7fff || (address >= 0xc000 && address <= 0xf7ff) || address >= 0xfc00) {
        return memory_[address];
    }
    if (address >= 0x8000 && address <= 0xbfff) {
        return bank_rom_[bank_ & 7][address & 0x3fff];
    }
    if (address >= 0xf800 && address <= 0xf9ff) {
        return palette_ram_[address & 0x1ff];
    }
    if (address == 0xfa00) return sound_stat_;
    return 0xff;
}

void BublBobl::main_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if ((address >= 0xc000 && address <= 0xf7ff) || address >= 0xfc00) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xf800 && address <= 0xf9ff) {
        const uint16_t idx = uint16_t(address & 0x1ff);
        if (palette_ram_[idx] != value) {
            palette_ram_[idx] = value;
            set_color(int(idx & 0x1fe) >> 1);
        }
        return;
    }
    if (address >= 0xfa00 && address <= 0xfa7f) {
        switch (address & 3) {
            case 0:
                if (!sound_nmi_) {
                    sound_latch_ = value;
                    sound_nmi_ = true;
                    if (!sound_reset_) sound_cpu_.set_nmi(IrqLine::Assert);
                }
                break;
            case 3:
                set_sound_reset(value != 0);
                break;
            default:
                break;
        }
        return;
    }
    if (address >= 0xfb00 && address <= 0xfb3f) {
        if (!sub_reset_) sub_cpu_.set_nmi(IrqLine::Pulse);
        return;
    }
    if (address >= 0xfb40 && address <= 0xfb7f) {
        bank_ = uint8_t((value ^ 4) & 7);
        set_sub_reset((value & 0x10) == 0);
        set_mcu_reset((value & 0x20) == 0);
        video_enable_ = (value & 0x40) != 0;
        flip_screen_ = (value & 0x80) != 0;
    }
}

uint8_t BublBobl::sub_read(uint16_t address) {
    if (address <= 0x7fff) return sub_rom_[address];
    if (address >= 0xe000 && address <= 0xf7ff) return memory_[address];
    return 0xff;
}

void BublBobl::sub_write(uint16_t address, uint8_t value) {
    if (address >= 0xe000 && address <= 0xf7ff) memory_[address] = value;
}

uint8_t BublBobl::sound_read(uint16_t address) {
    if (address <= 0x8fff) return sound_mem_[address];
    switch (address) {
        case 0x9000: return ym2203_.status();
        case 0x9001: return ym2203_.read();
        case 0xa000: return ym3526_.status();
        case 0xa001: return ym3526_.read();
        case 0xb000: return sound_latch_;
        default: return 0xff;
    }
}

void BublBobl::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;
    if (address >= 0x8000 && address <= 0x8fff) {
        sound_mem_[address] = value;
        return;
    }
    switch (address) {
        case 0x9000: ym2203_.control(value); break;
        case 0x9001: ym2203_.write(value); break;
        case 0xa000: ym3526_.control(value); break;
        case 0xa001: ym3526_.write(value); break;
        case 0xb000: sound_stat_ = value; break;
        case 0xb002:
            sound_nmi_ = false;
            sound_cpu_.set_nmi(IrqLine::Clear);
            break;
        default: break;
    }
}

void BublBobl::mcu_port1_write(uint8_t value) {
    if ((mcu_port1_out_ & 0x40) != 0 && (value & 0x40) == 0) {
        main_cpu_.set_irq(IrqLine::Assert, memory_[0xfc00]);
    }
    mcu_port1_out_ = value;
}

void BublBobl::mcu_port2_write(uint8_t value) {
    if ((mcu_port2_out_ & 0x10) == 0 && (value & 0x10) != 0) {
        const uint16_t address = uint16_t(mcu_port4_out_ | ((value & 0x0f) << 8));
        if ((mcu_port1_out_ & 0x80) != 0) {
            if ((address & 0x800) == 0) {
                switch (address & 3) {
                    case 0: mcu_port3_in_ = dsw_a_; break;
                    case 1: mcu_port3_in_ = dsw_b_; break;
                    case 2: mcu_port3_in_ = in1_; break;
                    case 3: mcu_port3_in_ = in2_; break;
                }
            } else if ((address & 0xc00) == 0xc00) {
                mcu_port3_in_ = memory_[0xfc00 + (address & 0x3ff)];
            }
        } else if ((address & 0xc00) == 0xc00) {
            memory_[0xfc00 + (address & 0x3ff)] = mcu_port3_out_;
        }
    }
    mcu_port2_out_ = value;
}

void BublBobl::draw_tile(int nchar, int color, bool flipx, bool flipy, int x, int y) {
    const uint8_t* src = gfx_.element(nchar);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            const int sx = flipx ? (7 - col) : col;
            const int sy = flipy ? (7 - row) : row;
            const uint8_t pen = src[sy * 8 + sx];
            if (pen == 15) continue;
            int dx = x + col;
            int dy = (y + row) & 0xff;
            if (dx < 0 || dx >= 256) continue;
            bitmap_[size_t(dy * 256 + dx)] = palette_[size_t((color + pen) & 0xff)];
        }
    }
}

void BublBobl::update_video() {
    const uint32_t backdrop = video_enable_ ? palette_[0xff] : 0xff000000u;
    std::fill(bitmap_.begin(), bitmap_.end(), backdrop);
    if (video_enable_) {
        int sx = 0;
        for (int offs = 0; offs < 0xc0; offs++) {
            const int base = 0xdd00 + offs * 4;
            if (memory_[size_t(base)] == 0 && memory_[size_t(base + 1)] == 0 &&
                memory_[size_t(base + 2)] == 0 && memory_[size_t(base + 3)] == 0) {
                continue;
            }
            const uint8_t gfx_num = memory_[size_t(base + 1)];
            const uint8_t gfx_attr = memory_[size_t(base + 3)];
            const int prom_line = 0x80 + ((gfx_num & 0xe0) >> 1);
            int gfx_offs = (gfx_num & 0x1f) << 7;
            if ((gfx_num & 0xa0) == 0xa0) gfx_offs |= 0x1000;
            // MAME: sy = -objectram[0]; Pascal used 256-value, which is the same
            // once Y is wrapped to 8 bits.
            const int sy = -int(memory_[size_t(base)]);

            for (int yc = 0; yc < 32; yc++) {
                const uint8_t atrib2 = prom_[size_t(prom_line + (yc >> 1))];
                if ((atrib2 & 8) != 0) continue;
                if ((atrib2 & 4) == 0) {
                    // Pascal added 256 when bit 6 was set, which parked those
                    // columns off the right of the 256-wide crop. MAME subtracts.
                    sx = int(memory_[size_t(base + 2)]);
                    if ((gfx_attr & 0x40) != 0) sx -= 256;
                }
                for (int xc = 0; xc < 2; xc++) {
                    const int goffs =
                        gfx_offs + (xc << 6) + ((yc & 7) << 1) + ((atrib2 & 3) << 4);
                    const uint8_t atrib = memory_[size_t(0xc001 + goffs)];
                    const int nchar = memory_[size_t(0xc000 + goffs)] + ((atrib & 3) << 8) +
                                      ((gfx_attr & 0x0f) << 10);
                    const int color = (atrib & 0x3c) << 2;
                    bool flipx = (atrib & 0x40) != 0;
                    bool flipy = (atrib & 0x80) != 0;
                    int x = sx + xc * 8;
                    int y = (sy + yc * 8) & 0xff;
                    if (flip_screen_) {
                        x = 248 - x;
                        y = 248 - y;
                        flipx = !flipx;
                        flipy = !flipy;
                    }
                    draw_tile(nchar, color, flipx, flipy, x, y);
                }
            }
            sx += 16;
        }
    }

    for (int row = 0; row < kScreenHeight; row++) {
        const uint32_t* src = bitmap_.data() + size_t(row + 16) * 256u;
        uint32_t* dst = framebuffer_.data() + size_t(row) * 256u;
        std::copy(src, src + 256, dst);
    }
}

}  // namespace dsp
