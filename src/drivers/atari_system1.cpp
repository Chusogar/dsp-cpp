#include "drivers/atari_system1.h"

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
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return bool(f);
}

bool try_load(const std::string& dir, const char* name, std::vector<uint8_t>& out) {
    namespace fs = std::filesystem;
    if (load_file((fs::path(dir) / name).string(), out)) return true;
    std::string upper = name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    return load_file((fs::path(dir) / upper).string(), out);
}

// Load interleaved 16-bit words from two odd/even ROM files (Atari style).
bool load_rom16(const std::string& dir, const char* even, const char* odd, uint16_t* dest, size_t words) {
    std::vector<uint8_t> e, o;
    if (!try_load(dir, even, e) || !try_load(dir, odd, o)) return false;
    const size_t n = std::min({e.size(), o.size(), words});
    for (size_t i = 0; i < n; ++i)
        dest[i] = uint16_t((e[i] << 8) | o[i]);  // big-endian 68k
    return n == words || n > 0;
}

uint32_t pal4bit_i(uint16_t v, uint16_t i) {
    // Atari intensity-enhanced 4-bit component
    int c = (v & 0xf) * 0x11;
    if (i & 8) c = std::min(255, c + 0x22);
    return uint32_t(c);
}

}  // namespace

AtariSystem1::AtariSystem1(Game game)
    : game_(game),
      main_cpu_(kCpuClock),
      snd_cpu_(kAudioClock),
      ym_(kYmClock),
      pokey_(kAudioClock),
      // Official dsp-cpp Slapstic; chip number set in init() per game.
      // 103=Peter Pack Rat, 105=Indiana Jones, 107=Marble Madness.
      slapstic_(107, &main_cpu_),
      via_(kAudioClock),
      tms_(640000) {}

const char* AtariSystem1::title() const {
    switch (game_) {
        case Game::PeterPak: return "Peter Pack Rat (Atari System 1)";
        case Game::Indy: return "Indiana Jones (Atari System 1)";
        default: return "Marble Madness (Atari System 1)";
    }
}

bool AtariSystem1::init(const std::string& rom_path, std::string* error) {
    // Minimal ROM load: expect a combined main ROM blob or paired files.
    // Bios / game ROMs vary; try common names then generic.
    std::vector<uint8_t> blob;
    rom_.fill(0);
    bool ok = false;

    if (try_load(rom_path, "main.bin", blob) || try_load(rom_path, "rom.bin", blob)) {
        const size_t words = std::min(blob.size() / 2, rom_.size());
        for (size_t i = 0; i < words; ++i)
            rom_[i] = uint16_t((blob[i * 2] << 8) | blob[i * 2 + 1]);
        ok = words > 0x1000;
    }
    // Interleaved pairs
    if (!ok) {
        ok = load_rom16(rom_path, "136032.136.e1", "136032.137.f1", rom_.data(), 0x10000);
    }
    if (!ok) {
        // Fill with NOP/reset vector stubs so machine can still construct
        rom_[0] = 0x0000;
        rom_[1] = 0x1000;  // SP
        rom_[2] = 0x0000;
        rom_[3] = 0x0400;  // PC
        if (error) *error = "warning: game ROMs not fully loaded from " + rom_path;
        // continue with partial
    }

    // Slapstic bank ROMs: copy from main region $80000 equivalent if present
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < 0x1000; ++i)
            slapstic_rom_[b][i] = rom_[0x4000 + b * 0x1000 + i];

    // Sound ROM
    snd_rom_.fill(0);
    std::vector<uint8_t> srom;
    if (try_load(rom_path, "sound.bin", srom) || try_load(rom_path, "136032.142.i7", srom)) {
        std::memcpy(snd_rom_.data(), srom.data(), std::min(srom.size(), snd_rom_.size()));
    }

    // Char / tile GFX — optional; solid tiles if missing
    tiles_.assign(256 * 64, 0);
    tile_count_ = 256;
    std::vector<uint8_t> chars;
    if (try_load(rom_path, "136032.104.f5", chars) || try_load(rom_path, "chars.bin", chars)) {
        // 2bpp 8x8 decode into 4-level tiles
        const int count = int(chars.size() / 16);
        tiles_.assign(size_t(count) * 64, 0);
        tile_count_ = count;
        for (int t = 0; t < count; ++t) {
            for (int y = 0; y < 8; ++y) {
                const uint8_t p0 = chars[t * 16 + y];
                const uint8_t p1 = chars[t * 16 + 8 + y];
                for (int x = 0; x < 8; ++x) {
                    const int bit = 7 - x;
                    tiles_[t * 64 + y * 8 + x] =
                        uint8_t(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1));
                }
            }
        }
    }

    for (int i = 0; i < 256; ++i) playfield_lookup_[i] = uint16_t(i & 0xff);

    // Slapstic type per game (atari_system1.pas)
    switch (game_) {
        case Game::PeterPak: slapstic_.set_type(103); break;
        case Game::Indy: slapstic_.set_type(105); break;
        default: slapstic_.set_type(107); break;  // Marble
    }

    main_cpu_.set_memory_handlers(
        [this](uint32_t a) { return cpu_read(a); },
        [this](uint32_t a, uint16_t v) { cpu_write(a, v); });
    main_cpu_.set_cycle_handler([this](int c) { on_main_cycles(c); });

    snd_cpu_.set_memory_handlers(
        [this](uint16_t a) { return snd_read(a); },
        [this](uint16_t a, uint8_t v) { snd_write(a, v); });
    snd_cpu_.set_cycle_handler([this](int c) { on_snd_cycles(c); });

    ym_.set_irq_handler([this](bool on) { snd_cpu_.set_irq(on ? IrqLine::Hold : IrqLine::Clear); });
    via_.set_irq_callback([this](IrqLine s) {
        // VIA IRQ OR'd onto 6502 IRQ with YM
        if (s == IrqLine::Hold) snd_cpu_.set_irq(IrqLine::Hold);
    });
    tms_.set_irq_callback([this](bool on) {
        // TMS READY/IRQ can pulse 6502 NMI or VIA CB1 — feed CB1
        via_.write_cb1(on);
    });

    reset();
    return true;
}

void AtariSystem1::reset() {
    slapstic_.reset();
    rom_bank_ = slapstic_.current_bank();
    main_cpu_.reset();
    snd_cpu_.reset();
    ym_.reset();
    pokey_.reset();
    via_.reset();
    tms_.reset();
    in0_ = 0xff6f & 0xff;  // simplified
    in0_ = 0x6f;
    in1_ = 0xff;
    in2_ = 0x87;
    scroll_x_ = scroll_y_ = scroll_y_latch_ = 0;
    vblank_ = 0x10;
    bankselect_ = 0;
    playfield_tile_bank_ = 0;
    write_eeprom_ = false;
    sound_pending_ = main_pending_ = false;
    main_latch_ = sound_latch_ = 0;
    line_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    snd_cycle_acc_ = 0;
    ram_.fill(0);
    ram2_.fill(0);
    ram3_.fill(0);
    palette_.fill(0);
    argb_pal_.fill(0xff000000);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000);
}

void AtariSystem1::set_dip_switch(int, uint8_t) {}

void AtariSystem1::set_inputs(const MachineInputs& in) {
    // Active-low system 1 inputs (simplified)
    uint8_t p1 = 0xff;
    if (in.player1.up) p1 &= ~0x01;
    if (in.player1.down) p1 &= ~0x02;
    if (in.player1.left) p1 &= ~0x04;
    if (in.player1.right) p1 &= ~0x08;
    if (in.player1.button1) p1 &= ~0x10;
    if (in.player1.button2) p1 &= ~0x20;
    in1_ = p1;
    if (in.coin1) in0_ &= ~0x01;
    else in0_ |= 0x01;
    if (in.player1.start) in0_ &= ~0x02;
    else in0_ |= 0x02;
}

void AtariSystem1::set_color(uint16_t index, uint16_t value) {
    palette_[index & 0x3ff] = value;
    const uint32_t r = pal4bit_i(value >> 8, value >> 12);
    const uint32_t g = pal4bit_i(value >> 4, value >> 12);
    const uint32_t b = pal4bit_i(value, value >> 12);
    argb_pal_[index & 0x1ff] = 0xff000000u | (r << 16) | (g << 8) | b;
}

uint16_t AtariSystem1::cpu_read(uint32_t addr) {
    addr &= 0xffffff;
    if (addr <= 0x7ffff) return rom_[(addr >> 1) & 0x3ffff];
    if (addr >= 0x80000 && addr <= 0x87fff) {
        const uint16_t off = uint16_t((addr & 0x1fff) >> 1);
        const uint16_t v = slapstic_rom_[rom_bank_ & 3][off & 0xfff];
        rom_bank_ = slapstic_.tweak(uint16_t((addr & 0x7fff) >> 1));
        return v;
    }
    if (addr == 0x2e0000) {
        // IRQ3 status
        return 0;  // simplified
    }
    if (addr >= 0x400000 && addr <= 0x401fff)
        return ram_[(addr & 0x1fff) >> 1];
    if (addr >= 0x900000 && addr <= 0x9fffff)
        return ram2_[(addr & 0xfffff) >> 1];
    if (addr >= 0xa00000 && addr <= 0xa03fff)
        return ram3_[(addr & 0x3fff) >> 1];
    if (addr >= 0xb00000 && addr <= 0xb007ff)
        return palette_[(addr & 0x7ff) >> 1];
    if (addr >= 0xf00000 && addr <= 0xf00fff)
        return eeprom_[(addr & 0xfff) >> 1];
    if (addr >= 0xf20000 && addr <= 0xf20007) return 0x00ff;
    if (addr >= 0xf40000 && addr <= 0xf4001f) return 0;
    if (addr >= 0xf60000 && addr <= 0xf60003) {
        return uint16_t(in0_ | vblank_ | (sound_pending_ ? 0x80 : 0));
    }
    if (addr == 0xfc0000) {
        main_pending_ = false;
        main_cpu_.set_irq(6, IrqLine::Clear);
        return main_latch_;
    }
    return 0xffff;
}

void AtariSystem1::cpu_write(uint32_t addr, uint16_t value) {
    addr &= 0xffffff;
    if (addr >= 0x80000 && addr <= 0x87fff) {
        rom_bank_ = slapstic_.tweak(uint16_t((addr & 0x7fff) >> 1));
        return;
    }
    if (addr >= 0x400000 && addr <= 0x401fff) {
        ram_[(addr & 0x1fff) >> 1] = value;
        return;
    }
    if (addr == 0x800000) {
        scroll_x_ = value;
        return;
    }
    if (addr == 0x820000) {
        scroll_y_latch_ = value;
        if (line_ < 240) scroll_y_ = uint16_t(value - (line_ + 1));
        else scroll_y_ = value;
        return;
    }
    if (addr == 0x860000) {
        const uint16_t diff = uint16_t(bankselect_ ^ value);
        if (diff & 0x04) playfield_tile_bank_ = uint8_t((value >> 2) & 1);
        if (diff & 0x80) {
            // sound CPU reset control
            if (!(value & 0x80)) snd_cpu_.reset();  // held in reset when bit clear
        }
        bankselect_ = value;
        return;
    }
    if (addr == 0x8a0000) {
        main_cpu_.set_irq(4, IrqLine::Clear);
        return;
    }
    if (addr == 0x8c0000) {
        write_eeprom_ = true;
        return;
    }
    if (addr >= 0x900000 && addr <= 0x9fffff) {
        ram2_[(addr & 0xfffff) >> 1] = value;
        return;
    }
    if (addr >= 0xa00000 && addr <= 0xa03fff) {
        ram3_[(addr & 0x3fff) >> 1] = value;
        return;
    }
    if (addr >= 0xb00000 && addr <= 0xb007ff) {
        set_color(uint16_t((addr & 0x7ff) >> 1), value);
        return;
    }
    if (addr >= 0xf00000 && addr <= 0xf00fff) {
        if (write_eeprom_) {
            eeprom_[(addr & 0xfff) >> 1] = uint8_t(value & 0xff);
            write_eeprom_ = false;
        }
        return;
    }
    if (addr == 0xfc0000) {
        sound_latch_ = uint8_t(value & 0xff);
        sound_pending_ = true;
        snd_cpu_.set_nmi(IrqLine::Hold);
        return;
    }
}

uint8_t AtariSystem1::snd_read(uint16_t addr) {
    if (addr <= 0x0fff) return snd_ram_[addr];
    if (addr >= 0x4000) return snd_rom_[addr];
    // VIA6522 at $1000-$100F
    if (addr >= 0x1000 && addr <= 0x100f) return via_.read(uint8_t(addr & 0x0f));
    // TMS5220 status often on VIA PB or discrete; expose at $1001 mirror optional
    if (addr == 0x1801) return ym_.status();
    if (addr == 0x1810) {
        sound_pending_ = false;
        snd_cpu_.set_nmi(IrqLine::Clear);
        return sound_latch_;
    }
    if (addr == 0x1820)
        return uint8_t(in2_ | (sound_pending_ ? 0x08 : 0) | (main_pending_ ? 0x10 : 0));
    if (addr >= 0x1870 && addr <= 0x187f) return pokey_.read(addr & 0x0f);
    // Some boards map TMS status at $1008-ish via VIA; also direct $1808 style
    if (addr == 0x1808) return tms_.status();
    return 0xff;
}

void AtariSystem1::snd_write(uint16_t addr, uint8_t value) {
    if (addr <= 0x0fff) {
        snd_ram_[addr] = value;
        return;
    }
    if (addr >= 0x1000 && addr <= 0x100f) {
        via_.write(uint8_t(addr & 0x0f), value);
        // PA write often clocks TMS data
        if ((addr & 0x0f) == 0x01) tms_.write_data(value);
        return;
    }
    if (addr == 0x1800) {
        ym_.select_register(value);
        return;
    }
    if (addr == 0x1801) {
        ym_.write(value);
        return;
    }
    if (addr == 0x1808) {
        tms_.write_data(value);
        return;
    }
    if (addr == 0x1810) {
        main_latch_ = value;
        main_pending_ = true;
        main_cpu_.set_irq(6, IrqLine::Hold);
        return;
    }
    if (addr >= 0x1870 && addr <= 0x187f) {
        pokey_.write(addr & 0x0f, value);
        return;
    }
}

void AtariSystem1::on_main_cycles(int cycles) {
    // Run sound CPU in proportion (main 7.16 MHz, sound 1.79 MHz → /4)
    snd_cycle_acc_ += cycles;
    while (snd_cycle_acc_ >= 4) {
        snd_cycle_acc_ -= 4;
        snd_cpu_.run(1);
        pokey_.run(1);
        via_.tick(1);
        tms_.tick(1);
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kCpuClock)) {
        audio_acc_ -= int64_t(kCpuClock);
        const int32_t y = ym_.update();
        const int32_t p = pokey_.update();
        const int32_t t = tms_.update();
        const int32_t mix = y + p + t;
        audio_.push_back(int16_t(std::clamp(mix, int32_t(-32768), int32_t(32767))));
    }
}

void AtariSystem1::on_snd_cycles(int) {}

void AtariSystem1::update_video() {
    // Playfield from ram2: 64x64 tilemap, 16-bit per tile
    // Simplified: render 42x30 visible tiles with scroll
    const int sx = scroll_x_ & 0x1ff;
    const int sy = scroll_y_ & 0x1ff;
    for (int y = 0; y < kScreenH; ++y) {
        const int ty = ((y + sy) >> 3) & 63;
        const int fy = (y + sy) & 7;
        for (int x = 0; x < kScreenW; ++x) {
            const int tx = ((x + sx) >> 3) & 63;
            const int fx = (x + sx) & 7;
            const int map_i = (ty * 64 + tx) & 0xfff;
            const uint16_t tile_attr = ram2_[map_i];
            const uint16_t lookup = playfield_lookup_[(tile_attr >> 8) & 0x7f];
            int code = (tile_attr & 0xff) | ((lookup & 0xff) << 0);
            code &= (tile_count_ > 0 ? tile_count_ - 1 : 0);
            const int color_base = 0x20 + (((lookup >> 12) & 0xf) << 2);
            uint8_t pen = 0;
            if (tile_count_ > 0 && code < tile_count_)
                pen = tiles_[code * 64 + fy * 8 + fx] & 0x0f;
            const uint32_t col = argb_pal_[(color_base + pen) & 0x1ff];
            framebuffer_[y * kScreenW + x] = col ? col : 0xff000000;
        }
    }
    // Motion objects: ram3 holds linked list — simplified sprite scan
    // Each MO entry 4 words; draw small 8x8 markers for non-zero codes
    for (int i = 0; i < 0x100; i += 4) {
        const uint16_t w0 = ram3_[i];
        const uint16_t w1 = ram3_[i + 1];
        const uint16_t w2 = ram3_[i + 2];
        if (w1 == 0 || w1 == 0xffff) continue;
        const int mx = (w2 >> 5) & 0x1ff;
        const int my = (w0 >> 5) & 0x1ff;
        const int code = w1 & 0xff;
        const int color = 0x100 + ((w1 >> 8) & 0x0f);
        if (code >= tile_count_) continue;
        for (int py = 0; py < 8; ++py) {
            const int yy = (my + py) & 0x1ff;
            if (yy >= kScreenH) continue;
            for (int px = 0; px < 8; ++px) {
                const int xx = (mx + px) & 0x1ff;
                if (xx >= kScreenW) continue;
                const uint8_t pen = tiles_[code * 64 + py * 8 + px] & 0x0f;
                if (pen) framebuffer_[yy * kScreenW + xx] = argb_pal_[(color + pen) & 0x1ff];
            }
        }
    }
}

void AtariSystem1::run_frame() {
    const int cycles_per_line = int(kCpuClock / kFps / kScanlines);
    for (line_ = 0; line_ < kScanlines; ++line_) {
        if (line_ == 0) {
            vblank_ = 0;
        }
        if (line_ == 240) {
            vblank_ = 0x10;
            main_cpu_.set_irq(4, IrqLine::Hold);
            update_video();
        }
        int left = cycles_per_line;
        while (left > 0) {
            const int ran = main_cpu_.run(left);
            if (ran <= 0) break;
            left -= ran;
        }
    }
    line_ = 0;
}

void AtariSystem1::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
