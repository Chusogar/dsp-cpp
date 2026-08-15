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
    rom_.fill(0);
    for (auto& b : slapstic_rom_) b.fill(0);
    snd_rom_.fill(0);
    tiles_.assign(0x200 * 64, 0);  // alpha chars
    tile_count_ = 0x200;

    auto load_interleaved = [&](const char* a, const char* b, uint16_t* dest, size_t words) -> bool {
        return load_rom16(rom_path, a, b, dest, words);
    };

    // --- BIOS (always required): 136032.205 / 136032.206 at $00000 ---
    // Interleaved even/odd bytes → 16-bit words, 0x4000 words (32KB)
    if (!load_interleaved("136032.205.l13", "136032.206.l12", rom_.data(), 0x4000) &&
        !load_interleaved("136032.205", "136032.206", rom_.data(), 0x4000)) {
        // try alternate bios names
        if (!load_interleaved("205.l13", "206.l12", rom_.data(), 0x4000)) {
            if (error) *error = "Atari System 1 BIOS not found (136032.205 / 136032.206)";
            return false;
        }
    }

    // Helper: load pairs into a byte buffer at even/odd offsets then copy as words
    auto load_pairs_to_rom = [&](const std::vector<std::pair<const char*, int>>& pairs,
                                 int rom_word_base) -> bool {
        // pairs: {filename, byte_offset in temp} — Atari 16-bit ROMs load as
        // consecutive even/odd files forming big-endian words.
        // Simpler path: consecutive (even,odd) file pairs at successive word offsets.
        return true;  // filled below per-game
    };

    bool game_ok = false;
    std::vector<uint8_t> tmp;

    if (game_ == Game::Marble) {
        // marble_rom: pairs at $0, $8000, $10000, $18000, slapstic at $20000
        struct Pair { const char* e; const char* o; int byte_off; };
        const Pair pairs[] = {
            {"136033.623", "136033.624", 0x00000},
            {"136033.625", "136033.626", 0x08000},
            {"136033.627", "136033.628", 0x10000},
            {"136033.129", "136033.630", 0x18000},
            {"136033.107", "136033.108", 0x20000},  // slapstic source region
        };
        // Also try full names with suffixes from some dumps
        auto try_pair = [&](const char* e, const char* o, uint16_t* dest, size_t words) {
            if (load_rom16(rom_path, e, o, dest, words)) return true;
            // short names already tried
            return false;
        };
        // Game code starts at $10000 (word $8000)
        game_ok = true;
        if (!try_pair("136033.623", "136033.624", &rom_[0x10000 >> 1], 0x4000)) game_ok = false;
        if (!try_pair("136033.625", "136033.626", &rom_[0x18000 >> 1], 0x4000)) game_ok = false;
        if (!try_pair("136033.627", "136033.628", &rom_[0x20000 >> 1], 0x4000)) game_ok = false;
        if (!try_pair("136033.129", "136033.630", &rom_[0x28000 >> 1], 0x4000)) {
            // alternate second odd name
            if (!try_pair("136033.129", "136033.130", &rom_[0x28000 >> 1], 0x4000))
                game_ok = false;
        }
        // Slapstic banks from 136033.107/108 — 8KB region → 4 banks of 4KB words? 
        // Pascal: copymemory slapstic from memoria_temp[$20000] as $2000 bytes each bank
        // Load 107/108 interleaved into temp then split
        std::array<uint16_t, 0x4000> slap_src{};
        if (try_pair("136033.107", "136033.108", slap_src.data(), 0x4000)) {
            for (int b = 0; b < 4; ++b)
                for (int i = 0; i < 0x1000; ++i)
                    slapstic_rom_[b][i] = slap_src[b * 0x1000 + i];
        } else {
            // fallback: slice from main rom image
            for (int b = 0; b < 4; ++b)
                for (int i = 0; i < 0x1000; ++i)
                    slapstic_rom_[b][i] = rom_[(0x30000 >> 1) + b * 0x1000 + i];
        }
        // Sound
        try_load(rom_path, "136033.421", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0x8000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        tmp.clear();
        try_load(rom_path, "136033.422", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0xc000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        slapstic_.set_type(103);
    } else if (game_ == Game::PeterPak) {
        game_ok = true;
        auto tp = [&](const char* e, const char* o, int rom_byte) {
            if (!load_rom16(rom_path, e, o, &rom_[rom_byte >> 1], 0x4000)) game_ok = false;
        };
        tp("136028.142", "136028.143", 0x10000);
        tp("136028.144", "136028.145", 0x18000);
        tp("136028.146", "136028.147", 0x20000);
        std::array<uint16_t, 0x4000> slap_src{};
        if (load_rom16(rom_path, "136028.148", "136028.149", slap_src.data(), 0x4000)) {
            for (int b = 0; b < 4; ++b)
                for (int i = 0; i < 0x1000; ++i)
                    slapstic_rom_[b][i] = slap_src[b * 0x1000 + i];
        }
        try_load(rom_path, "136028.101", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0x8000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        tmp.clear();
        try_load(rom_path, "136028.102", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0xc000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        slapstic_.set_type(103);
    } else {  // Indy
        game_ok = true;
        auto tp = [&](const char* e, const char* o, int rom_byte, size_t words) {
            if (!load_rom16(rom_path, e, o, &rom_[rom_byte >> 1], words)) game_ok = false;
        };
        tp("136036.432", "136036.431", 0x10000, 0x8000);
        tp("136036.434", "136036.433", 0x20000, 0x8000);
        tp("136036.456", "136036.457", 0x30000, 0x4000);
        std::array<uint16_t, 0x4000> slap_src{};
        if (load_rom16(rom_path, "136036.358", "136036.359", slap_src.data(), 0x4000)) {
            for (int b = 0; b < 4; ++b)
                for (int i = 0; i < 0x1000; ++i)
                    slapstic_rom_[b][i] = slap_src[b * 0x1000 + i];
        }
        try_load(rom_path, "136036.153", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0x4000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        tmp.clear();
        try_load(rom_path, "136036.154", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0x8000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        tmp.clear();
        try_load(rom_path, "136036.155", tmp);
        if (!tmp.empty()) std::memcpy(snd_rom_.data() + 0xc000, tmp.data(), std::min(tmp.size(), size_t(0x4000)));
        slapstic_.set_type(105);
    }

    // Alpha characters: 136032.104.f5 — 8x8, 2 planes, $200 tiles
    std::vector<uint8_t> chars;
    if (try_load(rom_path, "136032.104.f5", chars) || try_load(rom_path, "136032.104", chars)) {
        const int count = int(chars.size() / 16);
        tiles_.assign(size_t(std::max(count, 0x200)) * 64, 0);
        tile_count_ = std::max(count, 0x200);
        for (int t = 0; t < count; ++t) {
            // layout: plane0 row0..7, plane1 row0..7  OR  interleaved by row
            // Pascal pc_x: 0..3,8..11 — 4bpp packed in 16-bit words per row
            // Standard Atari System 1 alpha: 2bpp, 16 bytes/tile
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

    // Identity playfield lookup until proms are decoded
    for (int i = 0; i < 256; ++i) playfield_lookup_[i] = uint16_t(i & 0xff);

    // Wire CPUs
    main_cpu_.set_memory_handlers(
        [this](uint32_t a) { return cpu_read(a); },
        [this](uint32_t a, uint16_t v) { cpu_write(a, v); });
    main_cpu_.set_cycle_handler([this](int c) { on_main_cycles(c); });
    snd_cpu_.set_memory_handlers(
        [this](uint16_t a) { return snd_read(a); },
        [this](uint16_t a, uint8_t v) { snd_write(a, v); });

    ym_.set_irq_handler([this](bool on) { snd_cpu_.set_irq(on ? IrqLine::Hold : IrqLine::Clear); });
    via_.set_irq_callback([this](IrqLine s) {
        if (s == IrqLine::Hold) snd_cpu_.set_irq(IrqLine::Hold);
    });
    tms_.set_irq_callback([this](bool on) {
        (void)on;
    });

    if (!game_ok && error)
        *error = "warning: some game ROMs missing in " + rom_path + " (BIOS loaded)";

    reset();
    return true;  // allow boot with BIOS even if game partial
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
    // Layer 1: alpha (text/HUD) from ram3[$3000..] — 64x32 tilemap, transparent pen 0
    // Layer 2: playfield from ram2[0..] with scroll
    // Simplified MO sprites from ram3 low region

    // Clear to palette 0 (or near-black)
    const uint32_t bg = argb_pal_[0] ? argb_pal_[0] : 0xff000000u;
    std::fill(framebuffer_.begin(), framebuffer_.end(), bg);

    auto draw_tile = [&](int dx, int dy, int code, int color_base, bool opaque) {
        if (code < 0) return;
        if (tile_count_ > 0) code %= tile_count_;
        for (int py = 0; py < 8; ++py) {
            const int yy = dy + py;
            if (yy < 0 || yy >= kScreenH) continue;
            for (int px = 0; px < 8; ++px) {
                const int xx = dx + px;
                if (xx < 0 || xx >= kScreenW) continue;
                uint8_t pen = 0;
                if (tile_count_ > 0 && code < tile_count_)
                    pen = tiles_[size_t(code) * 64 + size_t(py * 8 + px)] & 0x0f;
                if (!opaque && pen == 0) continue;
                const uint32_t col = argb_pal_[(color_base + pen) & 0x1ff];
                framebuffer_[size_t(yy * kScreenW + xx)] = col ? col : 0xff101010u;
            }
        }
    };

    // Playfield 64x64, scrolled
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
            const uint16_t lookup = playfield_lookup_[(tile_attr >> 8) & 0x7f |
                                                     ((playfield_tile_bank_ & 1) << 7)];
            int code = ((lookup & 0xff) << 8) | (tile_attr & 0xff);
            if (tile_count_ > 0) code %= tile_count_;
            const int color_base = 0x20 + ((((lookup >> 12) & 0xf)) << 2);
            uint8_t pen = 0;
            if (tile_count_ > 0)
                pen = tiles_[size_t(code) * 64 + size_t(fy * 8 + fx)] & 0x0f;
            const uint32_t col = argb_pal_[(color_base + pen) & 0x1ff];
            if (pen || col)
                framebuffer_[size_t(y * kScreenW + x)] = col ? col : 0xff202020u;
        }
    }

    // Alpha / text layer: ram3 at $3000 (word index $1800), 64x32
    for (int f = 0; f < 0x800; ++f) {
        const int tx = f & 63;
        const int ty = f >> 6;
        const uint16_t atrib = ram3_[(0x3000 >> 1) + f];
        const int color = (atrib >> 10) & 7;
        const int code = atrib & 0x3ff;
        const bool opaque = (atrib & 0x2000) != 0;
        draw_tile(tx * 8, ty * 8, code, color << 2, opaque);
    }

    // Crude motion objects: linked list style entries in ram3 low
    for (int i = 0; i < 0x100; i += 4) {
        const uint16_t w0 = ram3_[i];
        const uint16_t w1 = ram3_[i + 1];
        const uint16_t w2 = ram3_[i + 2];
        if (w1 == 0 || w1 == 0xffff) continue;
        const int mx = (int(w2) >> 5) & 0x1ff;
        const int my = (int(w0) >> 5) & 0x1ff;
        const int code = w1 & 0xff;
        const int color = 0x100 + ((w1 >> 8) & 0x0f);
        draw_tile(mx, my, code, color, false);
    }
}

void AtariSystem1::run_frame() {
    // Pascal: 262 lines; at 239 vblank:=0 + IRQ4 + update_video; at 261 vblank:=$10
    const int cycles_per_line = int(kCpuClock / kFps / kScanlines);
    if (cycles_per_line < 1) {
        // fallback
    }
    const int cpl = std::max(1, cycles_per_line);
    for (line_ = 0; line_ < kScanlines; ++line_) {
        int left = cpl;
        while (left > 0) {
            const int ran = main_cpu_.run(left);
            if (ran <= 0) break;
            left -= ran;
        }
        if (line_ == 239) {
            vblank_ = 0x00;
            main_cpu_.set_irq(4, IrqLine::Hold);
            update_video();
        }
        if (line_ == 261) {
            vblank_ = 0x10;
        }
    }
    scroll_y_ = scroll_y_latch_;
    line_ = 0;
}

void AtariSystem1::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
