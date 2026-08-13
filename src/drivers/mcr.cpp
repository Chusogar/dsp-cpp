#include "drivers/mcr.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace dsp {
namespace {

bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return bool(f);
}

bool try_load(const std::string& dir, const char* name, std::vector<uint8_t>& out) {
    namespace fs = std::filesystem;
    if (load_file((fs::path(dir) / name).string(), out)) return true;
    std::string u = name;
    for (char& c : u) c = char(std::toupper(static_cast<unsigned char>(c)));
    return load_file((fs::path(dir) / u).string(), out);
}

uint32_t pal3bit(uint16_t v) {
    int c = (v & 7) * 36;
    return uint32_t(std::min(255, c));
}

// Decode planar 4bpp 8x8 from 4 bitplanes (MCR style)
void decode_chars(const uint8_t* src, size_t src_size, std::vector<uint8_t>& out, int count) {
    out.assign(size_t(count) * 64, 0);
    const size_t plane = src_size / 4;
    for (int t = 0; t < count; ++t) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                uint8_t pen = 0;
                for (int p = 0; p < 4; ++p) {
                    size_t off = plane * p + size_t(t) * 8 + y;
                    if (off < src_size && (src[off] >> bit) & 1) pen |= uint8_t(1 << p);
                }
                out[t * 64 + y * 8 + x] = pen;
            }
        }
    }
}

}  // namespace

Mcr::Mcr(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ay0_(kSoundClock),
      ay1_(kSoundClock) {}

const char* Mcr::title() const {
    switch (game_) {
        case Game::Tapper: return "Tapper (MCR)";
        case Game::Tron: return "Tron (MCR)";
        case Game::Shollow: return "Satan's Hollow (MCR)";
        case Game::Domino: return "Domino Man (MCR)";
        case Game::Wacko: return "Wacko (MCR)";
        case Game::Dotron: return "Discs of Tron (MCR)";
        case Game::Timber: return "Timber (MCR)";
    }
    return "MCR";
}

bool Mcr::load_roms(const std::string& path, std::string* error) {
    mem_.fill(0xff);
    sound_mem_.fill(0xff);
    // Generic: main.bin, sound.bin, chars.bin, sprites.bin
    std::vector<uint8_t> main, snd, chars, spr;
    if (try_load(path, "main.bin", main) || try_load(path, "cpu.bin", main)) {
        std::memcpy(mem_.data(), main.data(), std::min(main.size(), mem_.size()));
    } else {
        // Tapper-style multi ROM names (partial)
        const char* parts[] = {
            "tapper_c.ic10", "tapper_c.ic9", "tapper_c.ic8", "tapper_c.ic7",
            "136024.104", "136024.103", "136024.102", "136024.101"};
        size_t off = 0;
        for (const char* n : parts) {
            std::vector<uint8_t> p;
            if (try_load(path, n, p)) {
                std::memcpy(mem_.data() + off, p.data(), std::min(p.size(), size_t(0x4000)));
                off += 0x4000;
            }
        }
        if (off == 0) {
            if (error) *error = "main CPU ROM not found in " + path;
            return false;
        }
    }
    if (try_load(path, "sound.bin", snd) || try_load(path, "snd.bin", snd)) {
        std::memcpy(sound_mem_.data(), snd.data(), std::min(snd.size(), sound_mem_.size()));
    }
    if (try_load(path, "chars.bin", chars) || try_load(path, "bg.bin", chars)) {
        char_count_ = int(chars.size() / 32);  // approx 4 planes * 8
        if (char_count_ > 0) decode_chars(chars.data(), chars.size(), chars_, std::min(char_count_, 0x400));
        char_count_ = int(chars_.size() / 64);
    } else {
        chars_.assign(256 * 64, 0);
        char_count_ = 256;
    }
    if (try_load(path, "sprites.bin", spr) || try_load(path, "fg.bin", spr)) {
        // 32x32 4bpp ≈ 512 bytes/sprite if planar packed; store as 32*32 pens
        sprite_count_ = std::max(1, int(spr.size() / 512));
        sprites_.assign(size_t(sprite_count_) * 1024, 0);
        for (int s = 0; s < sprite_count_; ++s) {
            for (int i = 0; i < 1024 && size_t(s) * 512 + i / 2 < spr.size(); ++i) {
                uint8_t b = spr[size_t(s) * 512 + i / 2];
                sprites_[s * 1024 + i] = (i & 1) ? (b & 0x0f) : (b >> 4);
            }
        }
    } else {
        sprites_.assign(128 * 1024, 0);
        sprite_count_ = 128;
    }
    return true;
}

bool Mcr::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_io_handlers(
        [this](uint16_t p) { return main_in(p); },
        [this](uint16_t p, uint8_t v) { main_out(p, v); });
    main_cpu_.set_cycle_handler([this](int c) { on_main_cycles(c); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });

    ctc_.set_irq_callback([this](IrqLine s) { main_cpu_.set_irq(s); });

    reset();
    return true;
}

void Mcr::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ctc_.reset();
    ay0_.reset();
    ay1_.reset();
    nvram_.fill(0);
    in0_ = in1_ = in2_ = in3_ = 0xff;
    dsw_ = 0xc0;
    ssio_status_ = 0;
    ssio_data_.fill(0);
    ssio_count_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    sound_div_ = 0;
    palette_.fill(0xff000000);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000);
}

void Mcr::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Mcr::set_inputs(const MachineInputs& in) {
    auto pack = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.up) v &= ~0x01;
        if (p.down) v &= ~0x02;
        if (p.left) v &= ~0x04;
        if (p.right) v &= ~0x08;
        if (p.button1) v &= ~0x10;
        if (p.button2) v &= ~0x20;
        if (p.start) v &= ~0x40;
        return v;
    };
    in1_ = pack(in.player1);
    in2_ = pack(in.player2);
    in0_ = 0xff;
    if (in.coin1) in0_ &= ~0x01;
    if (in.coin2) in0_ &= ~0x02;
}

void Mcr::set_color(int index, uint16_t value) {
    const uint32_t r = pal3bit(value >> 6);
    const uint32_t g = pal3bit(value);
    const uint32_t b = pal3bit(value >> 3);
    palette_[index & 0xff] = 0xff000000u | (r << 16) | (g << 8) | b;
}

uint8_t Mcr::main_read(uint16_t addr) {
    // Tapper-style map
    if (addr <= 0xdfff) return mem_[addr];
    if (addr >= 0xe000 && addr <= 0xe7ff) return nvram_[addr & 0x7ff];
    if (addr >= 0xe800 && addr <= 0xebff) return mem_[0xe800 + (addr & 0x1ff)];
    if (addr >= 0xf000 && addr <= 0xf7ff) return mem_[addr];
    return 0xff;
}

void Mcr::main_write(uint16_t addr, uint8_t value) {
    if (addr <= 0xdfff) return;  // ROM
    if (addr >= 0xe000 && addr <= 0xe7ff) {
        nvram_[addr & 0x7ff] = value;
        return;
    }
    if (addr >= 0xe800 && addr <= 0xebff) {
        mem_[0xe800 + (addr & 0x1ff)] = value;  // sprite RAM
        return;
    }
    if (addr >= 0xf000 && addr <= 0xf7ff) {
        mem_[addr] = value;  // videoram
        return;
    }
    if (addr >= 0xf800) {
        set_color((addr & 0x7f) >> 1, uint16_t(value | ((addr & 1) << 8)));
    }
}

uint8_t Mcr::main_in(uint16_t port) {
    const uint8_t p = uint8_t(port & 0xff);
    if (p <= 0x1f) {
        switch (p & 7) {
            case 0: return in0_;
            case 1: return in1_;
            case 2: return in2_;
            case 3: return dsw_;
            case 4: return in3_;
            case 7: return ssio_status_;
            default: return 0xff;
        }
    }
    if (p >= 0xf0 && p <= 0xf3) return ctc_.read(uint8_t(p & 3));
    return 0xff;
}

void Mcr::main_out(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port & 0xff);
    if (p >= 0x1c && p <= 0x1f) {
        ssio_data_[p & 3] = value;
        return;
    }
    if (p >= 0xf0 && p <= 0xf3) {
        ctc_.write(uint8_t(p & 3), value);
        return;
    }
}

uint8_t Mcr::sound_read(uint16_t addr) {
    if (addr <= 0x3fff) return sound_mem_[addr];
    // SSIO status / data ports vary; expose latched commands
    if (addr >= 0x8000 && addr <= 0x8003) return ssio_data_[addr & 3];
    return 0xff;
}

void Mcr::sound_write(uint16_t addr, uint8_t value) {
    if (addr <= 0x3fff) {
        // ROM area mostly; some boards have RAM low
        if (addr >= 0x2000) sound_mem_[addr] = value;
        return;
    }
    // AY ports (typical SSIO)
    if ((addr & 0xf) == 0x00) ay0_.control(value);
    if ((addr & 0xf) == 0x01) ay0_.write(value);
    if ((addr & 0xf) == 0x02) ay1_.control(value);
    if ((addr & 0xf) == 0x03) ay1_.write(value);
    if (addr == 0xe000) ssio_status_ = value;
}

void Mcr::on_main_cycles(int cycles) {
    ctc_.tick(cycles);
    // Sound CPU at ~2 MHz, main at 5 MHz
    sound_div_ += cycles * 2;
    while (sound_div_ >= 5) {
        sound_div_ -= 5;
        sound_cpu_.run(1);
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kMainClock)) {
        audio_acc_ -= int64_t(kMainClock);
        const int32_t mix = ay0_.update() + ay1_.update();
        audio_.push_back(int16_t(std::clamp(mix, int32_t(-32768), int32_t(32767))));
    }
}

void Mcr::on_sound_cycles(int) {}

void Mcr::update_video() {
    // Background: 32x30 of 16x16 from 8x8 pairs in videoram $f000
    for (int ty = 0; ty < 30; ++ty) {
        for (int tx = 0; tx < 32; ++tx) {
            const int offs = (ty * 32 + tx) * 2;
            const uint8_t code = mem_[0xf000 + offs];
            const uint8_t attr = mem_[0xf001 + offs];
            const int color = (attr & 3) << 4;
            const int tile = code % std::max(1, char_count_);
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int sx = tx * 16 + x * 2;
                    const int sy = ty * 16 + y * 2;
                    if (sx >= kScreenW || sy >= kScreenH) continue;
                    const uint8_t pen = chars_[tile * 64 + y * 8 + x] & 0x0f;
                    const uint32_t c = palette_[(color + pen) & 0xff];
                    // 2x scale
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            framebuffer_[(sy + dy) * kScreenW + sx + dx] = c;
                }
            }
        }
    }
    // Sprites from $e800, 4 bytes × 128, 32×32, 2x scale positions
    for (int f = 0x7f; f >= 0; --f) {
        const int base = 0xe800 + f * 4;
        const int sy = ((241 - mem_[base]) * 2) & 0x1ff;
        const uint8_t attr = mem_[base + 1];
        const int code = (mem_[base + 2] + ((attr & 8) << 5)) % std::max(1, sprite_count_);
        const int sx = ((mem_[base + 3] - 3) * 2) & 0x1ff;
        const int color = ((~attr) & 3) << 4;
        const bool flipx = (attr & 0x10) != 0;
        const bool flipy = (attr & 0x20) != 0;
        for (int y = 0; y < 32; ++y) {
            const int yy = flipy ? (31 - y) : y;
            for (int x = 0; x < 32; ++x) {
                const int xx = flipx ? (31 - x) : x;
                const uint8_t pen = sprites_[code * 1024 + yy * 32 + xx] & 0x0f;
                if (!pen) continue;
                const int dx = (sx + x) & 0x1ff;
                const int dy = (sy + y) & 0x1ff;
                if (dx < kScreenW && dy < kScreenH)
                    framebuffer_[dy * kScreenW + dx] = palette_[(color + pen) & 0xff];
            }
        }
    }
}

void Mcr::run_frame() {
    const int cycles_line = int(kMainClock / kFps / kScanlines);
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 0) {
            ctc_.trigger(2);
        }
        if (line == kScanlines / 2) {
            ctc_.trigger(3);
            update_video();
        }
        int left = cycles_line;
        while (left > 0) {
            const int ran = main_cpu_.run(left);
            if (ran <= 0) break;
            left -= ran;
        }
    }
}

void Mcr::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
