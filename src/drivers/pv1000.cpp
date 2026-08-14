#include "drivers/pv1000.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(size_t(n));
    f.read(reinterpret_cast<char*>(out->data()), n);
    return bool(f);
}

}  // namespace

Pv1000::Pv1000()
    : cpu_(kCpuClock),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_mem(a); },
        [this](uint16_t a, uint8_t v) { write_mem(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return in_port(p); },
        [this](uint16_t p, uint8_t v) { out_port(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
}

bool Pv1000::init(const std::string& rom_path, std::string* error) {
    return load_media(rom_path, error);
}

bool Pv1000::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open ROM: " + path;
        return false;
    }
    rom_.fill(0xFF);
    const size_t n = std::min(data.size(), rom_.size());
    std::memcpy(rom_.data(), data.data(), n);
    // Mirror small carts into the 32 KB window.
    if (n > 0 && n < rom_.size()) {
        for (size_t i = n; i < rom_.size(); i++) rom_[i] = rom_[i % n];
    }
    reset();
    return true;
}

void Pv1000::reset() {
    cpu_.reset();
    tile_map_.fill(0);
    pattern_ram_.fill(0);
    dirty_.fill(true);
    io_ram_.fill(0);
    keys_.fill(0);
    force_pattern_ = false;
    fd_buffer_flag_ = false;
    fd_data_ = 0;
    pcg_bank_ = 0;
    border_col_ = 0;
    for (auto& v : voice_) {
        v.count = 0;
        v.period = 0;
        v.val = 1;
    }
    sound_control_ = 0;
    sound_out_ = 0;
    sound_phase_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    scanline_ = 0;
    std::fill(framebuffer_.begin(), framebuffer_.end(), kPalette[0]);
}

void Pv1000::set_inputs(const MachineInputs& inputs) {
    // keys[0]: coin1, start1, coin2, start2
    keys_[0] = uint8_t((inputs.coin1 ? 0x01 : 0) | (inputs.player1.start ? 0x02 : 0) |
                       (inputs.coin2 ? 0x04 : 0) | (inputs.player2.start ? 0x08 : 0));
    // keys[1]: down1, right1, down2, right2
    keys_[1] = uint8_t((inputs.player1.down ? 0x01 : 0) | (inputs.player1.right ? 0x02 : 0) |
                       (inputs.player2.down ? 0x04 : 0) | (inputs.player2.right ? 0x08 : 0));
    // keys[2]: left1, up1, left2, up2
    keys_[2] = uint8_t((inputs.player1.left ? 0x01 : 0) | (inputs.player1.up ? 0x02 : 0) |
                       (inputs.player2.left ? 0x04 : 0) | (inputs.player2.up ? 0x08 : 0));
    // keys[3]: but0_1, but1_1, but0_2, but1_2
    keys_[3] = uint8_t((inputs.player1.button1 ? 0x01 : 0) |
                       (inputs.player1.button2 ? 0x02 : 0) |
                       (inputs.player2.button1 ? 0x04 : 0) |
                       (inputs.player2.button2 ? 0x08 : 0));
}

void Pv1000::set_dip_switch(int /*bank*/, uint8_t /*value*/) {}

void Pv1000::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Memory / IO
// ---------------------------------------------------------------------------

uint8_t Pv1000::read_mem(uint16_t addr) {
    if (addr < 0x8000) return rom_[addr & 0x7FFF];
    if (addr >= 0xB800 && addr < 0xBC00) return tile_map_[addr & 0x3FF];
    if (addr >= 0xBC00 && addr <= 0xBFFF) return pattern_ram_[addr & 0x3FF];
    return 0xFF;
}

void Pv1000::write_mem(uint16_t addr, uint8_t value) {
    if (addr >= 0xB800 && addr < 0xBC00) {
        const uint16_t i = addr & 0x3FF;
        if (tile_map_[i] != value) {
            tile_map_[i] = value;
            dirty_[i] = true;
        }
        return;
    }
    if (addr >= 0xBC00 && addr <= 0xBFFF) {
        const uint16_t i = addr & 0x3FF;
        if (pattern_ram_[i] != value) {
            pattern_ram_[i] = value;
            // Any pattern change dirties the whole map (matches Pascal).
            dirty_.fill(true);
        }
    }
}

uint8_t Pv1000::in_port(uint16_t port) {
    switch (port & 0xFF) {
        case 0xF8:
        case 0xF9:
        case 0xFA:
        case 0xFB:
        case 0xFE:
        case 0xFF:
            return io_ram_[port & 7];
        case 0xFC: {
            uint8_t temp = fd_buffer_flag_ ? 1 : 0;
            if (fd_data_ != 0) temp |= 2;
            fd_buffer_flag_ = false;
            return temp;
        }
        case 0xFD: {
            uint8_t temp = 0;
            for (int f = 0; f < 4; f++) {
                if (io_ram_[5] & (1 << f)) {
                    temp |= keys_[f];
                    fd_data_ = uint8_t(fd_data_ & ~(1 << f));
                }
            }
            return temp;
        }
        default:
            return 0xFF;
    }
}

void Pv1000::out_port(uint16_t port, uint8_t value) {
    switch (port & 0xFF) {
        case 0xF8:
        case 0xF9:
        case 0xFA:
        case 0xFB: {
            io_ram_[port & 7] = value;
            const int ch = port & 3;
            if (ch <= 2) {
                const uint16_t per = uint16_t((~value) & 0x3F);
                if (per == 0 && voice_[ch].period != 0)
                    voice_[ch].val = uint8_t(~voice_[ch].val);
                voice_[ch].period = per;
            } else {
                sound_control_ = value;
            }
            break;
        }
        case 0xFC:
        case 0xFE:
            io_ram_[port & 7] = value;
            break;
        case 0xFD:
            io_ram_[port & 7] = value;
            fd_data_ = 0x0F;
            break;
        case 0xFF:
            io_ram_[port & 7] = value;
            pcg_bank_ = uint8_t((value & 0x20) >> 5);
            force_pattern_ = (value & 0x10) != 0;
            if (border_col_ != (value & 7)) {
                border_col_ = value & 7;
                // Repaint border rows immediately.
                const uint32_t c = kPalette[border_col_ & 7];
                for (int y = 0; y < kBorderTop; y++)
                    for (int x = 0; x < kScreenWidth; x++)
                        framebuffer_[size_t(y) * kScreenWidth + x] = c;
                for (int y = kBorderTop + kActiveHeight; y < kScreenHeight; y++)
                    for (int x = 0; x < kScreenWidth; x++)
                        framebuffer_[size_t(y) * kScreenWidth + x] = c;
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void Pv1000::draw_tile(int sx, int sy, uint16_t pattern_base) {
    // pattern_base points at byte 0 of a 32-byte tile; planes start at +8.
    // Three 1-bit planes (8 bytes each) → 3-bit colour index.
    for (int y = 0; y < 8; y++) {
        uint8_t p3 = 0, p2 = 0, p1 = 0;
        // Patterns can live in ROM ($0000+) or pattern RAM ($BC00).
        auto fetch = [this](uint16_t a) -> uint8_t {
            if (a >= 0xBC00 && a <= 0xBFFF) return pattern_ram_[a & 0x3FF];
            if (a < 0x8000) return rom_[a & 0x7FFF];
            return 0;
        };
        p3 = fetch(uint16_t(pattern_base + y));
        p2 = fetch(uint16_t(pattern_base + 8 + y));
        p1 = fetch(uint16_t(pattern_base + 16 + y));

        const int py = kBorderTop + sy * 8 + y;
        if (py < 0 || py >= kScreenHeight) continue;
        for (int x = 7; x >= 0; x--) {
            const uint8_t idx = uint8_t(((p1 >> x) & 1) | (((p2 >> x) & 1) << 1) |
                                        (((p3 >> x) & 1) << 2));
            // Screen columns 2..29 map to pixels 0..223 (skip ROM cols 0-1).
            const int px = (sx - 2) * 8 + (7 - x);
            if (px < 0 || px >= kActiveWidth) continue;
            framebuffer_[size_t(py) * kScreenWidth + px] = kPalette[idx & 7];
        }
    }
}

void Pv1000::update_video() {
    for (int sy = 0; sy < 24; sy++) {
        for (int sx = 2; sx <= 29; sx++) {
            const uint16_t addr = uint16_t(sy * 32 + sx);
            if (!dirty_[addr]) continue;
            uint16_t tile = tile_map_[addr];
            uint16_t pos;
            if (tile < 0xE0 || force_pattern_) {
                tile = uint16_t(tile | (pcg_bank_ << 8));
                pos = uint16_t(tile * 32 + 8);  // ROM patterns
            } else {
                tile = uint16_t(tile - 0xE0);
                pos = uint16_t(0xBC00 + tile * 32 + 8);  // RAM patterns
            }
            draw_tile(sx, sy, pos);
            dirty_[addr] = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

void Pv1000::sound_tick() {
    static constexpr int kVolumes[3] = {0x10, 0x18, 0x20};
    for (int f = 0; f < 3; f++) {
        voice_[f].count++;
        if (voice_[f].period > 0 && voice_[f].count >= voice_[f].period) {
            voice_[f].count = 0;
            voice_[f].val = uint8_t(~voice_[f].val);
        }
    }
    if ((sound_control_ & 2) == 0) {
        sound_out_ = 0;
        return;
    }
    int sum = 0;
    if (sound_control_ & 1) {
        const int xor01 = (voice_[0].val ^ voice_[1].val) & 1;
        const int xor12 = (voice_[1].val ^ voice_[2].val) & 1;
        (void)xor01;
        (void)xor12;
        // Pascal mixes voice levels with XOR mode still summing channels;
        // keep the same simple sum as the non-XOR path for audible output.
        sum += int(voice_[0].val & 1) * kVolumes[0] * 256;
        sum += int(voice_[1].val & 1) * kVolumes[1] * 256;
    } else {
        sum += int(voice_[0].val & 1) * kVolumes[0] * 256;
        sum += int(voice_[1].val & 1) * kVolumes[1] * 256;
    }
    sum += int(voice_[2].val & 1) * kVolumes[2] * 256;
    // Centre around 0
    sum = sum - (kVolumes[0] + kVolumes[1] + kVolumes[2]) * 128;
    if (sum > 32767) sum = 32767;
    if (sum < -32768) sum = -32768;
    sound_out_ = int16_t(sum);
}

void Pv1000::push_audio_samples(int cpu_cycles) {
    // Advance sound clock proportional to CPU time.
    // sound rate = 17897725/1024, CPU = 17897725/5 → sound ticks per CPU cycle = 5/1024
    sound_phase_ += double(cpu_cycles) * (5.0 / 1024.0);
    while (sound_phase_ >= 1.0) {
        sound_phase_ -= 1.0;
        sound_tick();
    }
    // Resample to 44100
    audio_acc_ += double(cpu_cycles) * double(kSampleRate);
    while (audio_acc_ >= double(kCpuClock)) {
        audio_acc_ -= double(kCpuClock);
        audio_.push_back(sound_out_);
    }
}

void Pv1000::on_cycles(int cycles) {
    push_audio_samples(cycles);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Pv1000::run_frame() {
    // IRQ schedule from pv1000_principal (scanlines 0..261).
    auto assert_irq = [this] { cpu_.set_irq(IrqLine::Assert); };
    auto clear_irq = [this] { cpu_.set_irq(IrqLine::Clear); };

    for (scanline_ = 0; scanline_ < kScanlines; scanline_++) {
        const int f = scanline_;

        // FD buffer flag + IRQ at line 20
        if (f == 20) {
            fd_buffer_flag_ = true;
            assert_irq();
        }

        // Periodic CLEAR edges
        switch (f) {
            case 221: case 225: case 229: case 233: case 239:
            case 243: case 247: case 251: case 253: case 259:
            case 1: case 5: case 9: case 13: case 17: case 21:
                clear_irq();
                break;
            default:
                break;
        }

        // VBlank start: render + IRQ
        if (f == 220) {
            update_video();
            assert_irq();
        }

        // Periodic ASSERT edges
        switch (f) {
            case 224: case 228: case 232: case 238: case 242:
            case 246: case 250: case 252: case 258:
            case 0: case 4: case 8: case 12: case 16:
                assert_irq();
                break;
            default:
                break;
        }

        int remaining = kCyclesPerLine;
        while (remaining > 0) {
            const int ran = cpu_.run(remaining);
            remaining -= ran;
            if (ran <= 0) break;
        }
    }
}

}  // namespace dsp
