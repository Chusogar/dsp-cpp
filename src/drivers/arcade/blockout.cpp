#include "drivers/arcade/blockout.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kSound = {{"bo29e3-0.bin", 0x8000, 0, 0x3ea01f78}};
const std::vector<RomEntry> kOki = {{"bo29e2-0.bin", 0x20000, 0, 0x15c5a99d}};
struct MainRom {
    const char* name;
    uint32_t crc;
};
constexpr MainRom kMainEven = {"bo29a0-2.bin", 0xb0103427};
constexpr MainRom kMainOdd = {"bo29a1-2.bin", 0x5984d5a2};

// Resistor-weighted 4-bit gun (0x0e, 0x1f, 0x43, 0x8f).
uint8_t gun(uint16_t nibble) {
    return uint8_t(0x0e * (nibble & 1) + 0x1f * (nibble >> 1 & 1) + 0x43 * (nibble >> 2 & 1) +
                   0x8f * (nibble >> 3 & 1));
}

}  // namespace

BlockOut::BlockOut()
    : rom_(0x40000, 0),
      videoram_(0x40000, 0),
      bitmap_(512u * 256u, 0x100),
      framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return read16(a); },
                                  [this](uint32_t a, uint16_t v) { write16(a, v); });
    main_cpu_.set_byte_handlers([this](uint32_t a) { return read8(a); },
                                [this](uint32_t a, uint8_t v) { write8(a, v); });
    main_cpu_.set_address_mask(0xffffff);
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool on) { sound_cpu_.set_irq(on ? IrqLine::Assert : IrqLine::Clear); });
}

bool BlockOut::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // 68000 program: two 128 KB ROMs, even and odd bytes.
    for (int half = 0; half < 2; ++half) {
        const MainRom& r = half == 0 ? kMainEven : kMainOdd;
        std::vector<uint8_t> data;
        if (!loader.try_read(r.name, data) || data.size() != 0x20000) {
            if (error) *error = std::string("missing or bad ROM ") + r.name;
            return false;
        }
        if (crc32_of(data.data(), data.size()) != r.crc)
            warnings_.push_back(std::string("CRC mismatch: ") + r.name);
        for (size_t i = 0; i < data.size(); ++i) rom_[i * 2 + size_t(half)] = data[i];
    }

    std::vector<uint8_t> sound(0x8000, 0);
    if (!loader.load(kSound, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    std::vector<uint8_t> oki(0x40000, 0);
    if (!loader.load(kOki, oki, error)) return false;
    oki_.set_rom(std::move(oki));

    for (const auto& w : loader.warnings()) warnings_.push_back(w);
    reset();
    return true;
}

void BlockOut::reset() {
    std::fill(videoram_.begin(), videoram_.end(), 0);
    std::fill(bitmap_.begin(), bitmap_.end(), 0x100);
    ram1_.fill(0);
    ram2_.fill(0);
    front_ram_.fill(0);
    ram3_.fill(0);
    pal_ram_.fill(0);
    palette_.fill(0xff000000u);
    front_color_ = 0;
    sound_ram_.fill(0);
    soundlatch_ = 0;
    main_debt_ = sound_debt_ = 0;
    audio_accum_ = oki_accum_ = 0;
    last_oki_ = 0;
    audio_.clear();
    sound_commands_ = 0;
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    oki_.reset();
}

// ---------------------------------------------------------------------------
// Main CPU bus (byte level, big-endian)

uint8_t BlockOut::read8(uint32_t a) {
    a &= 0xffffff;
    if (a < 0x40000) return rom_[a];
    if (a >= 0x100000 && a < 0x10000a) {
        if (a & 1) {
            switch (a & 0xe) {
                case 0x0: return p1_;
                case 0x2: return p2_;
                case 0x4: return system_;
                case 0x6: return dsw1_;
                case 0x8: return uint8_t(dsw2_ & (0x3f | buttons_a_));
            }
        }
        return 0xff;
    }
    if (a >= 0x180000 && a < 0x1c0000) return videoram_[a & 0x3ffff];
    if (a >= 0x1d4000 && a < 0x1e0000) return ram1_[a - 0x1d4000];
    if (a >= 0x1f4000 && a < 0x200000) return ram2_[a - 0x1f4000];
    if (a >= 0x200000 && a < 0x208000) return front_ram_[a & 0x7fff];
    if (a >= 0x208000 && a < 0x220000) return ram3_[a - 0x208000];
    if (a >= 0x280200 && a < 0x280600) return pal_ram_[a - 0x280200];
    return 0xff;
}

void BlockOut::write8(uint32_t a, uint8_t v) {
    a &= 0xffffff;
    if (a < 0x40000) return;
    if (a >= 0x100010 && a < 0x100018) {
        switch (a) {
            case 0x100010: case 0x100011: main_cpu_.set_irq(6, IrqLine::Clear); break;
            case 0x100012: case 0x100013: main_cpu_.set_irq(5, IrqLine::Clear); break;
            case 0x100015:
                soundlatch_ = v;
                ++sound_commands_;
                sound_cpu_.set_nmi(IrqLine::Pulse);
                break;
            default: break;  // 100016: unknown (sound CPU reset?)
        }
        return;
    }
    if (a >= 0x180000 && a < 0x1c0000) return write_videoram(a & 0x3ffff, v);
    if (a >= 0x1d4000 && a < 0x1e0000) { ram1_[a - 0x1d4000] = v; return; }
    if (a >= 0x1f4000 && a < 0x200000) { ram2_[a - 0x1f4000] = v; return; }
    if (a >= 0x200000 && a < 0x208000) { front_ram_[a & 0x7fff] = v; return; }
    if (a >= 0x208000 && a < 0x220000) { ram3_[a - 0x208000] = v; return; }
    if (a == 0x280002 || a == 0x280003) {
        front_color_ = (a & 1) ? uint16_t((front_color_ & 0xff00) | v)
                               : uint16_t((front_color_ & 0x00ff) | (v << 8));
        set_color(512, front_color_);
        return;
    }
    if (a >= 0x280200 && a < 0x280600) {
        const uint32_t o = a - 0x280200;
        pal_ram_[o] = v;
        const uint32_t w = o & ~1u;
        set_color(int(w >> 1), uint16_t(pal_ram_[w] << 8 | pal_ram_[w + 1]));
    }
}

uint16_t BlockOut::read16(uint32_t a) {
    return uint16_t(read8(a) << 8 | read8(a + 1));
}

void BlockOut::write16(uint32_t a, uint16_t v) {
    write8(a, uint8_t(v >> 8));
    write8(a + 1, uint8_t(v));
}

void BlockOut::write_videoram(uint32_t offset, uint8_t v) {
    if (videoram_[offset] == v) return;
    videoram_[offset] = v;
    const uint32_t xy = offset & 0x1ffff;  // y << 9 | x
    const uint8_t front = videoram_[xy];
    const uint8_t back = videoram_[0x20000 | xy];
    bitmap_[xy] = front ? front : uint16_t(0x100 | back);
}

void BlockOut::set_color(int index, uint16_t raw) {
    // xBGR_444
    palette_[size_t(index)] = 0xff000000u | uint32_t(gun(raw & 15)) << 16 |
                              uint32_t(gun(raw >> 4 & 15)) << 8 | gun(raw >> 8 & 15);
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t BlockOut::sound_read(uint16_t a) {
    if (a < 0x8000) return sound_rom_[a];
    if (a < 0x8800) return sound_ram_[a & 0x7ff];
    if (a == 0x8801) return ym_.status();
    if (a == 0x9800) return oki_.read();
    if (a == 0xa000) return soundlatch_;
    return 0xff;
}

void BlockOut::sound_write(uint16_t a, uint8_t v) {
    if (a >= 0x8000 && a < 0x8800) {
        sound_ram_[a & 0x7ff] = v;
        return;
    }
    switch (a) {
        case 0x8800: ym_.select_register(v); break;
        case 0x8801: ym_.write(v); break;
        case 0x9800: oki_.write(v); break;
        default: break;
    }
}

void BlockOut::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);
    oki_accum_ += int64_t(cycles) * oki_.sample_frequency();
    while (oki_accum_ >= int64_t(kSoundClock)) {
        oki_accum_ -= int64_t(kSoundClock);
        last_oki_ = oki_.update();
    }
    audio_accum_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        const int32_t s = ym_.update() * 9 / 10 + last_oki_ * 3 / 4;
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

// ---------------------------------------------------------------------------
// Video

void BlockOut::update_video() {
    const uint32_t overlay = palette_[512];
    for (int y = 0; y < kScreenHeight; ++y) {
        const int sy = y + kFirstVisibleLine;
        const uint16_t* src = &bitmap_[size_t(sy) * 512];
        const uint8_t* front = &front_ram_[size_t(sy & 0xff) * 128];
        uint32_t* dst = &framebuffer_[size_t(y) * kScreenWidth];
        for (int x = 0; x < kScreenWidth; ++x) {
            // Overlay word (y << 6) + (x >> 3); its low byte holds 8 pixels, MSB first.
            const uint8_t bits = front[(x >> 3) * 2 + 1];
            dst[x] = (bits >> (7 - (x & 7)) & 1) ? overlay : palette_[src[x]];
        }
    }
}

// ---------------------------------------------------------------------------

void BlockOut::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 0) main_cpu_.set_irq(5, IrqLine::Assert);
        if (line == 250) {  // end of the visible area
            main_cpu_.set_irq(6, IrqLine::Assert);
            update_video();
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
    }
}

void BlockOut::set_inputs(const MachineInputs& in) {
    auto pad = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.right) v &= ~0x01;
        if (p.left) v &= ~0x02;
        if (p.up) v &= ~0x04;
        if (p.down) v &= ~0x08;
        if (p.button4) v &= ~0x10;  // drop (on top of the stick)
        if (p.button2) v &= ~0x20;  // B
        if (p.button3) v &= ~0x40;  // C
        if (p.start) v &= ~0x80;
        return v;
    };
    p1_ = pad(in.player1);
    p2_ = pad(in.player2);
    // Button A of each player is wired to the unused DSW2 switches 7 and 8.
    buttons_a_ = 0xc0;
    if (in.player1.button1) buttons_a_ &= ~0x40;
    if (in.player2.button1) buttons_a_ &= ~0x80;
    uint8_t sys = 0xff;
    if (in.coin1) sys &= ~0x02;
    if (in.coin2) sys &= ~0x04;
    system_ = sys;
}

void BlockOut::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw1_ = value;
    else if (bank == 1) dsw2_ = value;
}

void BlockOut::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
