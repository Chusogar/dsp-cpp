#include "drivers/asteroid.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kProgramRoms = {
    {"035145-04e.ef2", 0x0800, 0x6800, 0xb503eaf7},
    {"035144-04e.h2", 0x0800, 0x7000, 0x25233192},
    {"035143-02.j2", 0x0800, 0x7800, 0x312caa02},
    {"035127-02.np3", 0x0800, 0x5000, 0x8b71fd9e},
};

const std::vector<RomEntry> kDvgProm = {
    {"034602-01.c8", 0x0100, 0, 0x97953db8},
};

void blend_pixel(uint32_t& dest, uint32_t color, int intensity) {
    if (intensity <= 0) return;
    if (intensity > 255) intensity = 255;
    const int r = int((color >> 16) & 0xff) * intensity / 255;
    const int g = int((color >> 8) & 0xff) * intensity / 255;
    const int b = int(color & 0xff) * intensity / 255;
    const int dr = std::max(int((dest >> 16) & 0xff), r);
    const int dg = std::max(int((dest >> 8) & 0xff), g);
    const int db = std::max(int(dest & 0xff), b);
    dest = 0xff000000u | (uint32_t(dr) << 16) | (uint32_t(dg) << 8) | uint32_t(db);
}

}  // namespace

Asteroid::Asteroid() {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
    dvg_.set_memory([this](uint16_t a) { return memory_[a & 0x7fff]; });
}

bool Asteroid::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> rom(0x8000, 0);
    if (!loader.load(kProgramRoms, rom, error)) return false;
    std::copy(rom.begin(), rom.end(), memory_.begin());

    std::vector<uint8_t> prom(0x100, 0);
    if (!loader.load(kDvgProm, prom, error)) return false;
    dvg_.set_prom(prom.data(), prom.size());

    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);

    reset();
    return true;
}

void Asteroid::reset() {
    std::memset(memory_.data(), 0, 0x5000);
    ram_[0].fill(0);
    ram_[1].fill(0);
    ram_bank_ = 0;
    in0_ = 0;
    in1_ = 0;
    leftover_ = 0;
    total_cycles_ = 0;
    next_nmi_ = kNmiPeriod;
    explode_ = 0;
    thump_ = 0;
    sound_bit_.fill(false);
    noise_lfsr_ = 1;
    tone_phase_.fill(0);
    explode_remain_ = 0;
    shot_remain_ = 0;
    audio_cycle_accum_ = 0;
    audio_.clear();
    dvg_.reset();
    cpu_.reset();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

void Asteroid::on_cycles(int cycles) {
    if (cycles <= 0) return;
    total_cycles_ += uint64_t(cycles);
    while (total_cycles_ >= next_nmi_) {
        cpu_.set_nmi(IrqLine::Pulse);
        next_nmi_ += kNmiPeriod;
    }
    mix_audio(cycles);
}

uint8_t Asteroid::read_byte(uint16_t address) {
    address &= 0x7fff;
    if (address <= 0x01ff || (address >= 0x4000 && address <= 0x47ff) ||
        (address >= 0x5000 && address <= 0x57ff) || address >= 0x6800) {
        return memory_[address];
    }
    if (address <= 0x02ff) return ram_[ram_bank_][address & 0xff];
    if (address <= 0x03ff) return ram_[1 - ram_bank_][address & 0xff];

    if (address >= 0x2000 && address <= 0x2007) {
        if (address == 0x2001) return (total_cycles_ & 0x100) ? 0x80 : 0x7f;
        if (address == 0x2002) return dvg_.done() ? 0x80 : 0x00;
        const uint8_t mask = uint8_t(1u << (address & 7));
        return (in0_ & mask) ? 0x80 : 0x7f;
    }
    if (address >= 0x2400 && address <= 0x2407) {
        const uint8_t mask = uint8_t(1u << (address & 7));
        return (in1_ & mask) ? 0x80 : 0x7f;
    }
    if (address >= 0x2800 && address <= 0x2803) {
        const int shift = 6 - 2 * int(address & 3);
        return uint8_t(0xfc | ((dsw_ >> shift) & 3));
    }
    return 0;
}

void Asteroid::write_byte(uint16_t address, uint8_t value) {
    address &= 0x7fff;
    if (address <= 0x01ff || (address >= 0x4000 && address <= 0x47ff)) {
        memory_[address] = value;
        return;
    }
    if (address <= 0x02ff) {
        ram_[ram_bank_][address & 0xff] = value;
        return;
    }
    if (address <= 0x03ff) {
        ram_[1 - ram_bank_][address & 0xff] = value;
        return;
    }
    if (address == 0x3000) {
        dvg_.go();
        return;
    }
    if (address == 0x3200) {
        ram_bank_ = uint8_t((value & 4) >> 2);
        return;
    }
    if (address == 0x3600) {
        explode_ = value;
        explode_remain_ = kSampleRate / 3;
        return;
    }
    if (address == 0x3a00) {
        thump_ = value;
        return;
    }
    if (address >= 0x3c00 && address <= 0x3c05) {
        const int bit = int(address & 7);
        sound_bit_[size_t(bit)] = (value & 0x80) != 0 || (value & 0x01) != 0;
        if (bit == 4 && sound_bit_[4]) shot_remain_ = kSampleRate / 18;
        return;
    }
}

void Asteroid::run_frame() {
    const double cycles_per_line = double(kCpuClock) / kFramesPerSecond / double(kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        leftover_ += cycles_per_line;
        const int run = int(leftover_);
        leftover_ -= run;
        if (run > 0) cpu_.run(run);
    }
    update_video();
}

void Asteroid::update_video() {
    for (uint32_t& pixel : framebuffer_) {
        const int r = int((pixel >> 16) & 0xff) * 6 / 10;
        const int g = int((pixel >> 8) & 0xff) * 6 / 10;
        const int b = int(pixel & 0xff) * 6 / 10;
        pixel = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
    for (const Dvg::Line& line : dvg_.lines()) {
        draw_line(line.x0, line.y0, line.x1, line.y1, line.intensity);
    }
}

void Asteroid::draw_line(int x0, int y0, int x1, int y1, int intensity) {
    if (intensity <= 0) return;
    const int level = std::clamp(intensity * 17, 0, 255);
    const uint32_t color = 0xffffffffu;
    auto plot = [&](int x, int y, int i) {
        if (x >= 0 && x < kScreenWidth && y >= 0 && y < kScreenHeight) {
            blend_pixel(framebuffer_[size_t(y * kScreenWidth + x)], color, i);
        }
    };
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    const int glow = level / 3;
    while (true) {
        plot(x, y, level);
        if (glow > 0) {
            plot(x - 1, y, glow);
            plot(x + 1, y, glow);
            plot(x, y - 1, glow);
            plot(x, y + 1, glow);
        }
        if (x == x1 && y == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

void Asteroid::mix_audio(int cpu_cycles) {
    audio_cycle_accum_ += int64_t(cpu_cycles) * kSampleRate;
    while (audio_cycle_accum_ >= int64_t(kCpuClock)) {
        audio_cycle_accum_ -= kCpuClock;
        const uint32_t bit = ((noise_lfsr_ >> 0) ^ (noise_lfsr_ >> 1) ^ (noise_lfsr_ >> 4) ^
                              (noise_lfsr_ >> 15)) &
                             1u;
        noise_lfsr_ = (noise_lfsr_ >> 1) | (bit << 15);
        int sample = 0;

        if (sound_bit_[3]) sample += int(noise_lfsr_ & 1) * 5000 - 2500;

        if (explode_remain_ > 0) {
            const int vol = int((explode_ >> 2) & 0x0f) + 4;
            sample += (int(noise_lfsr_ & 0xff) - 128) * vol * 6;
            explode_remain_--;
        }

        if (thump_ & 0x10) {
            const int div = (thump_ & 0x0f) + 1;
            tone_phase_[6] += uint32_t(40 + div * 12);
            sample += (tone_phase_[6] & 0x8000) ? 4500 : -4500;
        }

        if (sound_bit_[0]) {
            const int freq = sound_bit_[2] ? 737 : 441;
            tone_phase_[0] += uint32_t(freq * 65536.0 / kSampleRate);
            sample += (tone_phase_[0] & 0x8000) ? 2800 : -2800;
        }
        if (sound_bit_[1]) {
            tone_phase_[1] += uint32_t(2800 * 65536.0 / kSampleRate);
            sample += (tone_phase_[1] & 0x8000) ? 2200 : -2200;
        }
        if (shot_remain_ > 0) {
            sample += (int(noise_lfsr_ & 1) * 2 - 1) * 5500;
            shot_remain_--;
        }
        if (sound_bit_[5]) {
            tone_phase_[5] += uint32_t(1500 * 65536.0 / kSampleRate);
            sample += (tone_phase_[5] & 0x8000) ? 3500 : -3500;
        }

        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Asteroid::set_inputs(const MachineInputs& inputs) {
    in0_ = 0;
    in1_ = 0;
    if (inputs.player1.button2) in0_ |= 0x08;  // hyperspace
    if (inputs.player1.button1) in0_ |= 0x10;  // fire
    if (inputs.coin1) in1_ |= 0x01;
    if (inputs.coin2) in1_ |= 0x02;
    if (inputs.player1.start) in1_ |= 0x08;
    if (inputs.player2.start) in1_ |= 0x10;
    if (inputs.player1.button3 || inputs.player1.up) in1_ |= 0x20;  // thrust
    if (inputs.player1.right) in1_ |= 0x40;
    if (inputs.player1.left) in1_ |= 0x80;
}

void Asteroid::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Asteroid::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
