#include "drivers/arcade/blueprint.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

struct GameRoms {
    std::vector<RomEntry> main, sound, chars, sprites;
    uint8_t dsw_a, dsw_b;
};

const GameRoms kBlueprint = {
    {{"bp-1.1m", 0x1000, 0x0000, 0xb20069a6}, {"bp-2.1n", 0x1000, 0x1000, 0x4a30302e},
     {"bp-3.1p", 0x1000, 0x2000, 0x6866ca07}, {"bp-4.1r", 0x1000, 0x3000, 0x5d3cfac3},
     {"bp-5.1s", 0x1000, 0x4000, 0xa556cac4}},
    {{"snd-1.3u", 0x1000, 0x0000, 0xfd38777a}, {"snd-2.3v", 0x1000, 0x2000, 0x33d5bf5b}},
    {{"bg-1.3c", 0x1000, 0x0000, 0xac2a61bc}, {"bg-2.3d", 0x1000, 0x1000, 0x81fe85d7}},
    {{"red.17d", 0x1000, 0x0000, 0xa73b6483}, {"blue.18d", 0x1000, 0x1000, 0x7d622550},
     {"green.20d", 0x1000, 0x2000, 0x2fcb4f26}},
    0xc3, 0xd5,
};

const GameRoms kSaturn = {
    {{"r1", 0x1000, 0x0000, 0x18a6d68e}, {"r2", 0x1000, 0x1000, 0xa7dd2665},
     {"r3", 0x1000, 0x2000, 0xb9cfa791}, {"r4", 0x1000, 0x3000, 0xc5a997e7},
     {"r5", 0x1000, 0x4000, 0x43444d00}, {"r6", 0x1000, 0x5000, 0x4d4821f6}},
    {{"r7", 0x1000, 0x0000, 0xdd43e02f}, {"r8", 0x1000, 0x2000, 0x7f9d0877}},
    {{"r10", 0x1000, 0x0000, 0x35987d61}, {"r9", 0x1000, 0x1000, 0xca6a7fda}},
    {{"r11", 0x1000, 0x0000, 0x6e4e6e5d}, {"r12", 0x1000, 0x1000, 0x46fc049e},
     {"r13", 0x1000, 0x2000, 0x8b3e8c32}},
    0x3d, 0xfd,
};

const GameRoms kGrasspin = {
    {{"prom_1.4b", 0x1000, 0x0000, 0x6fd50509}, {"jaleco-2.4c", 0x1000, 0x1000, 0xcd319007},
     {"jaleco-3.4d", 0x1000, 0x2000, 0xac73ccc2}, {"jaleco-4.4f", 0x1000, 0x3000, 0x41f6279d},
     {"jaleco-5.4h", 0x1000, 0x4000, 0xd20aead9}},
    {{"jaleco-6.4j", 0x1000, 0x0000, 0xf58bf3b0}, {"jaleco-7.4l", 0x1000, 0x2000, 0x2d587653}},
    {{"jaleco-9.4p", 0x1000, 0x0000, 0xbccca24c}, {"jaleco-8.3p", 0x1000, 0x1000, 0x9d6185ca}},
    {{"jaleco-10.5p", 0x1000, 0x0000, 0x3a0765c6}, {"jaleco-11.6p", 0x1000, 0x1000, 0xcccfbeb4},
     {"jaleco-12.7p", 0x1000, 0x2000, 0x615b3299}},
    0x7f, 0x9f,
};

const GameRoms& roms_for(Blueprint::Game g) {
    switch (g) {
        case Blueprint::Game::Saturn: return kSaturn;
        case Blueprint::Game::Grasspin: return kGrasspin;
        default: return kBlueprint;
    }
}

}  // namespace

Blueprint::Blueprint(Game game)
    : game_(game),
      back_(256u * 256u, 0xff000000u),
      front_(256u * 256u, 0),
      screen_(256u * 256u, 0xff000000u),
      framebuffer_(size_t(kWidth) * kHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });
    // AY 0: port A latches the DIP value read at c003, port B is the command latch.
    ay0_.set_port_handlers(nullptr, [this] { return sound_latch_; }, [this](uint8_t v) { dip_latch_ = v; },
                           nullptr);
    // AY 1: the two DIP banks.
    ay1_.set_port_handlers([this] { return dsw_a_; }, [this] { return dsw_b_; }, nullptr, nullptr);

    // Palette (MAME blueprnt_palette): no PROM. Characters combine two 3-bit
    // fields of the colour code selected by the pen bits; sprites are the
    // eight colours directly. Bit 3 dims the colour.
    for (int i = 0; i < 0x208; ++i) {
        int pen;
        if (i < 0x200) {
            const int bit0 = (i & 2) ? (i & 0xe0) >> 5 : 0;
            const int bit1 = (i & 1) ? (i & 0x1c) >> 2 : 0;
            pen = ((i & 0x100) >> 5) | bit0 | bit1;
        } else {
            pen = i - 0x200;
        }
        const uint32_t level = (pen & 8) ? 0xbf : 0xff;
        const uint32_t r = (pen & 1) ? level : 0, g = (pen & 4) ? level : 0, b = (pen & 2) ? level : 0;
        palette_[size_t(i)] = 0xff000000u | r << 16 | g << 8 | b;
    }
}

const char* Blueprint::title() const {
    switch (game_) {
        case Game::Saturn: return "Saturn";
        case Game::Grasspin: return "Grasspin";
        default: return "Blue Print";
    }
}

bool Blueprint::init(const std::string& rom_path, std::string* error) {
    const GameRoms& roms = roms_for(game_);
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x8000, 0xff);
    if (!loader.load(roms.main, main, error)) return false;
    std::copy(main.begin(), main.end(), rom_.begin());
    std::vector<uint8_t> sound(0x4000, 0xff);
    if (!loader.load(roms.sound, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    std::vector<uint8_t> chars(0x2000, 0);
    if (!loader.load(roms.chars, chars, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 0x200;
        l.planes = 2;
        l.char_increment = 8 * 8;
        l.plane_offsets = {0x1000 * 8, 0};
        l.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
        for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 8);
        chars_.decode(l, chars);
    }
    std::vector<uint8_t> sprites(0x3000, 0);
    if (!loader.load(roms.sprites, sprites, error)) return false;
    {
        GfxLayout l;
        l.width = 8;
        l.height = 16;
        l.total = 0x100;
        l.planes = 3;
        l.char_increment = 16 * 8;
        l.plane_offsets = {0x2000 * 8, 0x1000 * 8, 0};
        l.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
        for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 8);
        sprites_.decode(l, sprites);
    }
    dsw_a_ = roms.dsw_a;
    dsw_b_ = roms.dsw_b;

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Blueprint::reset() {
    ram_.fill(0);
    video_ram_.fill(0);
    color_ram_.fill(0);
    scroll_ram_.fill(0);
    sprite_ram_.fill(0);
    sound_ram_.fill(0);
    in0_ = in1_ = 0;
    sound_latch_ = dip_latch_ = gfx_bank_ = 0;
    flip_ = false;
    main_debt_ = sound_debt_ = 0;
    audio_accum_ = 0;
    sound_irq_counter_ = 0;
    sound_nmis_ = 0;
    audio_.clear();
    main_cpu_.reset();
    sound_cpu_.reset();
    ay0_.reset();
    ay1_.reset();
}

// ---------------------------------------------------------------------------
// Main CPU

uint8_t Blueprint::main_read(uint16_t a) {
    if (a < 0x8000) return rom_[a];
    if (a < 0x8800) return ram_[a & 0x7ff];
    if (a >= 0x9000 && a < 0xa000) return video_ram_[a & 0x3ff];
    if (a >= 0xa000 && a < 0xa100) return scroll_ram_[a & 0xff];
    if (a >= 0xb000 && a < 0xb100) return sprite_ram_[a & 0xff];
    if (a >= 0xf000) return color_ram_[a & 0x3ff];
    switch (a) {
        case 0xc000: return in0_;
        case 0xc001: return in1_;
        case 0xc003:
            // The sound CPU puts the DIP value here through AY 0's port A.
            return game_ == Game::Grasspin ? uint8_t((dip_latch_ & 0x7f) | 0x80) : dip_latch_;
        default: return 0;  // e000 watchdog
    }
}

void Blueprint::main_write(uint16_t a, uint8_t v) {
    if (a < 0x8000) return;
    if (a < 0x8800) {
        ram_[a & 0x7ff] = v;
    } else if (a >= 0x9000 && a < 0xa000) {
        video_ram_[a & 0x3ff] = v;
    } else if (a >= 0xa000 && a < 0xa100) {
        scroll_ram_[a & 0xff] = v;
    } else if (a >= 0xb000 && a < 0xb100) {
        sprite_ram_[a & 0xff] = v;
    } else if (a >= 0xf000) {
        color_ram_[a & 0x3ff] = v;
    } else if (a == 0xd000) {
        sound_latch_ = v;
        ++sound_nmis_;
        sound_cpu_.set_nmi(IrqLine::Pulse);
    } else if (a == 0xe000) {
        flip_ = (v & 2) == 0;
        gfx_bank_ = uint8_t((v & 4) >> 2);
    }
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t Blueprint::sound_read(uint16_t a) {
    if (a < 0x2000) return sound_rom_[a & 0xfff];
    if (a < 0x4000) return sound_rom_[0x2000 + (a & 0xfff)];
    if (a < 0x4400) return sound_ram_[a & 0x3ff];
    if (a == 0x6002) return ay0_.read();
    if (a == 0x8002) return ay1_.read();
    return 0xff;
}

void Blueprint::sound_write(uint16_t a, uint8_t v) {
    if (a >= 0x4000 && a < 0x4400) {
        sound_ram_[a & 0x3ff] = v;
        return;
    }
    switch (a) {
        case 0x6000: ay0_.control(v); break;
        case 0x6001: ay0_.write(v); break;
        case 0x8000: ay1_.control(v); break;
        case 0x8001: ay1_.write(v); break;
        default: break;
    }
}

void Blueprint::on_sound_cycles(int cycles) {
    // Four interrupts a frame from a timer on the sound board (1.25 MHz / 4 / 60).
    constexpr int kIrqPeriod = int(kSoundClock / (4 * 60));
    sound_irq_counter_ += cycles;
    while (sound_irq_counter_ >= kIrqPeriod) {
        sound_irq_counter_ -= kIrqPeriod;
        sound_cpu_.set_irq(IrqLine::Hold);
    }
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        // Two PSGs at full scale would clip: mix them at half level each.
        const int32_t s = (ay0_.update() + ay1_.update()) / 2;
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

// ---------------------------------------------------------------------------
// Video

void Blueprint::update_video() {
    // Tilemap: 32x32 characters stored by columns, right to left. The bank of
    // a character comes from bit 6 of the attribute 32 entries back (the tile
    // to its left in the stored order); bit 7 puts it in front of the sprites.
    for (int f = 0; f < 0x400; ++f) {
        const uint8_t attr = color_ram_[size_t(f)];
        const bool bank = (color_ram_[size_t((f - 32) & 0x3ff)] & 0x40) != 0;
        int code = video_ram_[size_t(f)];
        if (bank) code += gfx_bank_ * 0x100;
        const int base = (attr & 0x7f) * 4;
        const int tx = (31 - (f >> 5)) * 8, ty = (f & 31) * 8;
        const uint8_t* g = chars_.element(code);
        const bool front = (attr & 0x80) != 0;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int pen = g[r * 8 + c];
                const size_t at = size_t(ty + r) * 256 + size_t(tx + c);
                back_[at] = palette_[size_t(base + pen)];
                front_[at] = (front && pen != 0) ? palette_[size_t(base + pen)] : 0;
            }
    }

    // Each 8-pixel column scrolls vertically on its own.
    int scroll[32];
    for (int col = 0; col < 32; ++col) scroll[col] = scroll_ram_[size_t((30 - col) & 0xff)];
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            screen_[size_t(y) * 256 + size_t(x)] = back_[size_t((y + scroll[x >> 3]) & 255) * 256 + size_t(x)];

    // Sprites: 8x16, three bitplanes. The vertical flip of a sprite is held in
    // the attribute byte of the previous one (MAME notes the same oddity).
    bool flip_y = false;
    for (int f = 0; f < 0x40; ++f) {
        const uint8_t* s = &sprite_ram_[size_t(f) * 4];
        const int sx = 2 + s[3];
        const int sy = 240 - s[0] - 1;
        const uint8_t* g = sprites_.element(s[1]);
        const bool flip_x = (s[2] & 0x40) != 0;
        for (int r = 0; r < 16; ++r) {
            const int y = sy + r;
            if (y < 0 || y >= 256) continue;
            const uint8_t* row = g + (flip_y ? 15 - r : r) * 8;
            for (int c = 0; c < 8; ++c) {
                const int x = sx + c;
                if (x < 0 || x >= 256) continue;
                const int pen = row[flip_x ? 7 - c : c];
                if (pen) screen_[size_t(y) * 256 + size_t(x)] = palette_[size_t(0x200 + pen)];
            }
        }
        flip_y = (s[2] & 0x80) != 0;
    }

    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const uint32_t p = front_[size_t((y + scroll[x >> 3]) & 255) * 256 + size_t(x)];
            if (p) screen_[size_t(y) * 256 + size_t(x)] = p;
        }

    // Visible rows 16..239, flipped for the cocktail table's second player,
    // then turned 270 degrees for the vertical monitor.
    for (int oy = 0; oy < kHeight; ++oy)
        for (int ox = 0; ox < kWidth; ++ox) {
            int row = 16 + ox, col = 255 - oy;
            if (flip_) {
                row = 255 - row;
                col = 255 - col;
            }
            framebuffer_[size_t(oy) * kWidth + size_t(ox)] = screen_[size_t(row) * 256 + size_t(col)];
        }
}

// ---------------------------------------------------------------------------

void Blueprint::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 239) {
            main_cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
    }
}

void Blueprint::set_inputs(const MachineInputs& in) {
    const bool button2 = game_ != Game::Blueprint;  // Saturn and Grasspin use two buttons
    auto pad = [&](const InputState& p, bool coin) {
        uint8_t v = 0;
        if (coin) v |= 0x01;
        if (p.start) v |= 0x02;
        if (button2 && p.button2) v |= 0x04;
        if (p.button1) v |= 0x08;
        if (p.left) v |= 0x10;
        if (p.right) v |= 0x20;
        if (p.up) v |= 0x40;
        if (p.down) v |= 0x80;
        return v;
    };
    in0_ = pad(in.player1, in.coin1);
    in1_ = pad(in.player2, in.coin2);
}

void Blueprint::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

void Blueprint::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
