#include "drivers/arcade/cps1.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rom_loader.h"
#include "machine/kabuki.h"

namespace dsp {
namespace {

constexpr uint8_t kSprites = Cps1::kGfxSprites;
constexpr uint8_t kScroll1 = Cps1::kGfxScroll1;
constexpr uint8_t kScroll2 = Cps1::kGfxScroll2;
constexpr uint8_t kScroll3 = Cps1::kGfxScroll3;
constexpr uint8_t kStars = Cps1::kGfxStars;

const std::array<Cps1::CpsB, 13> kCpsB = {{
    {0x166, 0x170, 0x1ff, 0x0000, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x02, 0x04, 0x08, 0x30, 0x168, 0x16a, 0x16c, 0x16e},
    {0x16e, 0x16a, 0x160, 0x0004, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x02, 0x04, 0x08, 0x00, 0x166, 0x170, 0x168, 0x172},
    {0x160, 0x170, 0x1ff, 0x0000, 0x15e, 0x15c, 0x15a, 0x158, 0x30, 0x08, 0x30, 0x00, 0x16e, 0x16c, 0x16a, 0x168},
    {0x166, 0x170, 0x172, 0x0401, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x08, 0x10, 0x20, 0x00, 0x168, 0x16a, 0x16c, 0x16e},
    {0x168, 0x170, 0x172, 0x0800, 0x14e, 0x14c, 0x14a, 0x148, 0x20, 0x04, 0x08, 0x12, 0x166, 0x164, 0x162, 0x160},
    {0x160, 0x170, 0x1ff, 0x0000, 0x146, 0x144, 0x142, 0x140, 0x20, 0x12, 0x12, 0x00, 0x16e, 0x16c, 0x16a, 0x168},
    {0x168, 0x170, 0x1ff, 0x0000, 0x146, 0x144, 0x142, 0x140, 0x20, 0x10, 0x82, 0x00, 0x166, 0x164, 0x162, 0x160},
    {0x166, 0x170, 0x172, 0x0000, 0x140, 0x142, 0x144, 0x146, 0x02, 0x04, 0x08, 0x30, 0x168, 0x16a, 0x16c, 0x16e},
    {0x14a, 0x144, 0x1ff, 0x0000, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x16, 0x16, 0x16, 0x00, 0x14c, 0x14e, 0x140, 0x142},
    {0x152, 0x14c, 0x14e, 0x0c00, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x04, 0x02, 0x20, 0x00, 0x154, 0x156, 0x148, 0x14a},
    {0x170, 0x166, 0x164, 0x0003, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x20, 0x10, 0x08, 0x00, 0x16e, 0x16c, 0x16a, 0x168},
    {0x168, 0x172, 0x160, 0x0005, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x02, 0x08, 0x20, 0x14, 0x16a, 0x16c, 0x16e, 0x170},
    {0x142, 0x14c, 0x14e, 0x0405, 0x1ff, 0x1ff, 0x1ff, 0x1ff, 0x04, 0x02, 0x20, 0x00, 0x144, 0x146, 0x148, 0x14a},
}};

const std::array<Cps1::BankMap, 14> kBanks = {{
    // DM620 Ghouls
    {{0x8000, 0x2000, 0x2000, 0},
     {{{kScroll3, 0x8000, 0xbfff, 1},
       {kSprites, 0x2000, 0x3fff, 2},
       {uint8_t(kStars | kSprites | kScroll1 | kScroll2 | kScroll3), 0, 0x1ffff, 0}}}},
    // S224B Final Fight
    {{0x8000, 0, 0, 0},
     {{{kSprites, 0x0000, 0x43ff, 0},
       {kScroll1, 0x4400, 0x4bff, 0},
       {kScroll3, 0x4c00, 0x5fff, 0},
       {kScroll2, 0x6000, 0x7fff, 0}}}},
    // KD29B
    {{0x8000, 0x8000, 0, 0},
     {{{kSprites, 0x0000, 0x7fff, 0},
       {kSprites, 0x8000, 0x8fff, 1},
       {kScroll2, 0x9000, 0xbfff, 1},
       {kScroll1, 0xc000, 0xd7ff, 1},
       {kScroll3, 0xd800, 0xffff, 1}}}},
    // STF29 SF2
    {{0x8000, 0x8000, 0x8000, 0},
     {{{kSprites, 0x0000, 0x7fff, 0},
       {kSprites, 0x8000, 0xffff, 1},
       {kSprites, 0x10000, 0x11fff, 2},
       {kScroll3, 0x2000, 0x3fff, 2},
       {kScroll1, 0x4000, 0x4fff, 2},
       {kScroll2, 0x5000, 0x7fff, 2}}}},
    // ST24M1 Strider
    {{0x8000, 0x8000, 0, 0},
     {{{kStars, 0x0000, 0x03ff, 0},
       {kSprites, 0x0000, 0x4fff, 0},
       {kScroll2, 0x4000, 0x7fff, 0},
       {kScroll3, 0x0000, 0x7fff, 1},
       {kScroll1, 0x7000, 0x7fff, 1}}}},
    // RT24B
    {{0x8000, 0x8000, 0, 0},
     {{{kSprites, 0x0000, 0x53ff, 0},
       {kScroll1, 0x5400, 0x6fff, 0},
       {kScroll3, 0x7000, 0x7fff, 0},
       {kScroll3, 0x0000, 0x3fff, 1},
       {kScroll2, 0x2800, 0x7fff, 1},
       {kSprites, 0x5400, 0x7fff, 1}}}},
    // CC63B
    {{0x8000, 0x8000, 0, 0},
     {{{kSprites, 0x0000, 0x7fff, 0},
       {kScroll2, 0x0000, 0x7fff, 0},
       {kSprites, 0x8000, 0xffff, 1},
       {kScroll1, 0x8000, 0xffff, 1},
       {kScroll2, 0x8000, 0xffff, 1},
       {kScroll3, 0x8000, 0xffff, 1}}}},
    // KR63B
    {{0x8000, 0x8000, 0, 0},
     {{{kSprites, 0x0000, 0x7fff, 0},
       {kScroll2, 0x0000, 0x7fff, 0},
       {kScroll1, 0x8000, 0x9fff, 1},
       {kSprites, 0x8000, 0xcfff, 1},
       {kScroll2, 0x8000, 0xcfff, 1},
       {kScroll3, 0xd000, 0xffff, 1}}}},
    // S9263B SF2CE
    {{0x8000, 0x8000, 0x8000, 0},
     {{{kSprites, 0x0000, 0x7fff, 0},
       {kSprites, 0x8000, 0xffff, 1},
       {kSprites, 0x10000, 0x11fff, 2},
       {kScroll3, 0x2000, 0x3fff, 2},
       {kScroll1, 0x4000, 0x4fff, 2},
       {kScroll2, 0x5000, 0x7fff, 2}}}},
    // CD63B
    {{0x8000, 0x8000, 0, 0},
     {{{kScroll1, 0x0000, 0x0fff, 0},
       {kSprites, 0x1000, 0x7fff, 0},
       {uint8_t(kSprites | kScroll2), 0x8000, 0xdfff, 1},
       {kScroll3, 0xe000, 0xffff, 1}}}},
    // PS63B
    {{0x8000, 0x8000, 0, 0},
     {{{kScroll1, 0x0000, 0x0fff, 0},
       {kSprites, 0x1000, 0x7fff, 0},
       {uint8_t(kSprites | kScroll2), 0x8000, 0xdbff, 1},
       {kScroll3, 0xdc00, 0xffff, 1}}}},
    // WL24B
    {{0x8000, 0x4000, 0, 0},
     {{{kSprites, 0x0000, 0x4fff, 0},
       {kScroll3, 0x5000, 0x6fff, 0},
       {kScroll1, 0x7000, 0x7fff, 0},
       {kScroll2, 0x0000, 0x3fff, 1}}}},
    // YI24B 1941
    {{0x8000, 0, 0, 0},
     {{{kSprites, 0x0000, 0x1fff, 0},
       {kScroll3, 0x2000, 0x3fff, 0},
       {kScroll1, 0x4000, 0x4fff, 0},
       {kScroll2, 0x4800, 0x7fff, 0}}}},
    // NM24B
    {{0x8000, 0, 0, 0},
     {{{kSprites, 0x0000, 0x3fff, 0},
       {kScroll2, 0x0000, 0x3fff, 0},
       {kScroll1, 0x4000, 0x47ff, 0},
       {kSprites, 0x4800, 0x67ff, 0},
       {kScroll2, 0x4800, 0x67ff, 0},
       {kScroll3, 0x6800, 0x7fff, 0}}}},
}};

const int kPt2X[32] = {4,  0,  12, 8,  20, 16, 28, 24, 36, 32, 44, 40, 52, 48, 60, 56,
                       68, 64, 76, 72, 84, 80, 92, 88, 100, 96, 108, 104, 116, 112, 124, 120};

// gfx RAM is 0x18000 words. 0x17fff is not a contiguous bitmask (bit 15 is
// clear), so `& 0x17fff` aliases 0xa000 to 0x2000 and breaks the palette.
constexpr uint32_t kVramWords = 0x18000;

uint32_t vram_index(uint32_t word) { return word % kVramWords; }

uint32_t vram_index_from_byte(uint32_t byte_addr) { return (byte_addr >> 1) % kVramWords; }

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    return loader.load(entries, dest, error);
}

bool load_16b(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> temp(entry.length);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, temp, error)) return false;
        size_t need = size_t(entry.offset) + size_t(entry.length) * 2;
        if (dest.size() < need) dest.resize(need, 0);
        for (uint32_t i = 0; i < entry.length; i++) dest[size_t(entry.offset) + i * 2] = temp[i];
    }
    return true;
}

bool load_swap_word(RomLoader& loader, const std::vector<RomEntry>& entries,
                    std::vector<uint8_t>& dest, std::string* error) {
    if (!loader.load(entries, dest, error)) return false;
    for (const RomEntry& entry : entries) {
        uint8_t* p = dest.data() + entry.offset;
        for (uint32_t i = 0; i < entry.length / 2; i++) std::swap(p[i * 2], p[i * 2 + 1]);
    }
    return true;
}

bool load_64b(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> temp(entry.length);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, temp, error)) return false;
        size_t need = size_t(entry.offset) + size_t(entry.length / 2) * 8 + 1;
        if (dest.size() < need) dest.resize(need, 0);
        uint8_t* p = dest.data() + entry.offset;
        for (uint32_t i = 0; i < entry.length / 2; i++) {
            p[0] = temp[i * 2];
            p[1] = temp[i * 2 + 1];
            p += 8;
        }
    }
    return true;
}

bool load_64b_b(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
                std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> temp(entry.length);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, temp, error)) return false;
        size_t need = size_t(entry.offset) + size_t(entry.length) * 8;
        if (dest.size() < need) dest.resize(need, 0);
        uint8_t* p = dest.data() + entry.offset;
        for (uint32_t i = 0; i < entry.length; i++) {
            *p = temp[i];
            p += 8;
        }
    }
    return true;
}

GfxLayout char_layout(int total, bool odd) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets.assign(kPt2X + (odd ? 8 : 0), kPt2X + (odd ? 16 : 8));
    layout.y_offsets = {0 * 64, 1 * 64, 2 * 64, 3 * 64, 4 * 64, 5 * 64, 6 * 64, 7 * 64};
    return layout;
}

GfxLayout tile16_layout(int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 128 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets.assign(kPt2X, kPt2X + 16);
    layout.y_offsets = {0 * 64,  1 * 64,  2 * 64,  3 * 64,  4 * 64,  5 * 64,  6 * 64,  7 * 64,
                        8 * 64,  9 * 64,  10 * 64, 11 * 64, 12 * 64, 13 * 64, 14 * 64, 15 * 64};
    return layout;
}

GfxLayout tile32_layout(int total) {
    GfxLayout layout;
    layout.width = 32;
    layout.height = 32;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 512 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets.assign(kPt2X, kPt2X + 32);
    layout.y_offsets.resize(32);
    for (int i = 0; i < 32; i++) layout.y_offsets[size_t(i)] = i * 128;
    return layout;
}

}  // namespace

Cps1::Cps1(Game game)
    : game_(game),
      main_cpu_(kMainClockFast),
      sound_cpu_(uses_qsound() ? kZ80ClockQsound : kZ80ClockYm),
      ym_(kZ80ClockYm),
      oki_(1000000, true),
      qsound_(0x200000) {
    if (rotate_final()) {
        screen_width_ = kScreenHeight;
        screen_height_ = kScreenWidth;
    }
    rom_.assign(0x100000, 0);
    scroll1_.assign(448 * 248, 0);
    scroll2_.assign(1024 * 1024, 0);
    scroll3_.assign(480 * 320, 0);
    priority_.assign(1024 * 1024, 0);
    composite_.assign(kWorkSize * kWorkSize, 0);
    framebuffer_.assign(size_t(screen_width_ * screen_height_), 0);
    qsnd_opcode_.assign(0x8000, 0);
    qsnd_data_.assign(0x8000, 0);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    if (uses_qsound()) {
        sound_cpu_.set_memory_handlers(
            [this](uint16_t a) { return qsound_z80_read(a); },
            [this](uint16_t a, uint8_t v) { qsound_z80_write(a, v); });
        sound_cpu_.set_opcode_read([this](uint16_t a) { return qsound_z80_opcode(a); });
    } else {
        sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                       [this](uint16_t a, uint8_t v) { sound_write(a, v); });
        ym_.set_irq_handler([this](bool state) {
            sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
        });
    }
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
}

const char* Cps1::title() const {
    switch (game_) {
        case Game::Ghouls: return "Ghouls'n Ghosts";
        case Game::Ffight: return "Final Fight";
        case Game::Kod: return "The King of Dragons";
        case Game::Sf2: return "Street Fighter II";
        case Game::Strider: return "Strider";
        case Game::Wonder3: return "Three Wonders";
        case Game::Captcomm: return "Captain Commando";
        case Game::Knights: return "Knights of the Round";
        case Game::Sf2ce: return "Street Fighter II' Champion Edition";
        case Game::Dino: return "Cadillacs and Dinosaurs";
        case Game::Punisher: return "The Punisher";
        case Game::Willow: return "Willow";
        case Game::Ca1941: return "1941: Counter Attack";
        case Game::Nemo: return "Nemo";
    }
    return "CPS1";
}

bool Cps1::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void Cps1::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    if (uses_qsound()) {
        qsound_.reset();
        eeprom_.reset();
    } else {
        ym_.reset();
        oki_.reset();
        oki_.set_pin7(true);
    }
    in0_ = in1_ = in2_ = 0xffff;
    sound_latch_ = sound_latch2_ = sound_bank_ = 0;
    scroll_x1_ = scroll_y1_ = scroll_x2_ = scroll_y2_ = scroll_x3_ = scroll_y3_ = 0;
    stars_x1_ = stars_x2_ = stars_y1_ = stars_y2_ = 0;
    cps1_frame_ = 0;
    cps1_sprites_ = cps1_scroll1_ = cps1_scroll2_ = cps1_scroll3_ = cps1_pal_ = 0xffff;
    cps1_rowscroll_ = 0;
    cps1_rowscrollstart_ = 0;
    cps1_mula_ = cps1_mulb_ = cps1_layer_ = cps1_palcltr_ = 0;
    pal_change_ = false;
    scroll_pri_x_ = scroll_pri_y_ = 0;
    sprites_pri_draw_ = false;
    rowscroll_ena_ = false;
    flip_screen_ = false;
    palette_dirty_.fill(1);
    pri_mask0_ = pri_mask1_ = pri_mask2_ = pri_mask3_ = 0;
    calc_mask(0, 0);
    calc_mask(0, 1);
    calc_mask(0, 2);
    calc_mask(0, 3);
    audio_accumulator_ = oki_accumulator_ = qsound_accumulator_ = qsound_irq_accumulator_ = 0;
    last_oki_ = 0;
    audio_.clear();
}

void Cps1::set_inputs(const MachineInputs& inputs) {
    in0_ = in1_ = in2_ = 0xffff;
    auto p1 = inputs.player1;
    auto p2 = inputs.player2;
    if (p1.right) in1_ = uint16_t(in1_ & 0xfffe);
    if (p1.left) in1_ = uint16_t(in1_ & 0xfffd);
    if (p1.down) in1_ = uint16_t(in1_ & 0xfffb);
    if (p1.up) in1_ = uint16_t(in1_ & 0xfff7);
    if (p1.button1) in1_ = uint16_t(in1_ & 0xffef);
    if (p1.button2) in1_ = uint16_t(in1_ & 0xffdf);
    if (p1.button3) in1_ = uint16_t(in1_ & 0xffbf);
    if (p2.right) in1_ = uint16_t(in1_ & 0xfeff);
    if (p2.left) in1_ = uint16_t(in1_ & 0xfdff);
    if (p2.down) in1_ = uint16_t(in1_ & 0xfbff);
    if (p2.up) in1_ = uint16_t(in1_ & 0xf7ff);
    if (p2.button1) in1_ = uint16_t(in1_ & 0xefff);
    if (p2.button2) in1_ = uint16_t(in1_ & 0xdfff);
    if (p2.button3) in1_ = uint16_t(in1_ & 0xbfff);
    if (inputs.coin1) in0_ = uint16_t(in0_ & 0xfeff);
    if (inputs.coin2) in0_ = uint16_t(in0_ & 0xfdff);
    if (p1.start) in0_ = uint16_t(in0_ & 0xefff);
    if (p2.start) in0_ = uint16_t(in0_ & 0xdfff);
}

void Cps1::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) dsw_c_ = value;
}

void Cps1::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void Cps1::run_frame() {
    const int main_cycles = int(double(main_clock()) / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(double(sound_clock()) / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            main_cpu_.set_irq(2, IrqLine::Hold);
            update_video();
            uint32_t base = cps1_sprites_;
            for (int i = 0; i < 0x400; i++) {
                sprite_buffer_[size_t(i)] = vram_[vram_index(base + uint32_t(i))];
            }
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
    cps1_frame_++;
}

void Cps1::on_sound_cycles(int cycles) {
    const uint32_t clock = sound_clock();
    if (uses_qsound()) {
        qsound_irq_accumulator_ += cycles * 250;
        while (qsound_irq_accumulator_ >= int64_t(clock)) {
            qsound_irq_accumulator_ -= clock;
            sound_cpu_.set_irq(IrqLine::Hold);
        }
        qsound_accumulator_ += int64_t(cycles) * (4000000 / QSound::kClockDiv);
        while (qsound_accumulator_ >= int64_t(clock)) {
            qsound_accumulator_ -= clock;
            qsound_.clock();
        }
        audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
        while (audio_accumulator_ >= int64_t(clock)) {
            audio_accumulator_ -= clock;
            audio_.push_back(int16_t(std::clamp(qsound_.mixed(), -32768, 32767)));
        }
        return;
    }
    int64_t ym_cycles = int64_t(cycles) * kZ80ClockYm / int64_t(clock);
    ym_.run_timers(int(ym_cycles));
    oki_accumulator_ += int64_t(cycles) * oki_.sample_frequency();
    while (oki_accumulator_ >= int64_t(clock)) {
        oki_accumulator_ -= clock;
        last_oki_ = oki_.update();
    }
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= int64_t(clock)) {
        audio_accumulator_ -= clock;
        int32_t sample = ym_.update() + int32_t(last_oki_ * 0.8f);
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

uint16_t Cps1::read_io(uint16_t dir) const {
    uint16_t res = 0xffff;
    switch (dir) {
        case 0:
        case 2:
        case 4:
        case 6: res = in1_; break;
        case 0x18: res = in0_; break;
        case 0x1a: res = uint16_t((dsw_a_ << 8) | 0xff); break;
        case 0x1c: res = uint16_t((dsw_b_ << 8) | 0xff); break;
        case 0x1e: res = uint16_t((dsw_c_ << 8) | 0xff); break;
        case 0x176: res = in2_; break;
        default: break;
    }
    const CpsB& b = kCpsB[size_t(cps_b_index_)];
    if (dir == b.testaddr) res = b.testval;
    uint32_t product = uint32_t(cps1_mula_) * uint32_t(cps1_mulb_);
    if (dir == b.mull) res = uint16_t(product);
    if (dir == b.mulh) res = uint16_t(product >> 16);
    return res;
}

void Cps1::write_io(uint16_t dir, uint16_t value) {
    auto object_base = [](uint16_t val) { return uint32_t(val) * 256u - 0x900000u; };
    switch (dir) {
        case 0x100: cps1_sprites_ = object_base(value) >> 1; break;
        case 0x102: cps1_scroll1_ = (object_base(value) & ~0x3fffu); break;
        case 0x104: cps1_scroll2_ = (object_base(value) & ~0x3fffu); break;
        case 0x106: cps1_scroll3_ = (object_base(value) & ~0x3fffu); break;
        case 0x108: cps1_rowscroll_ = object_base(value) >> 1; break;
        case 0x10a:
            cps1_pal_ = object_base(value) & 0x1ffff;
            palette_dirty_.fill(1);
            pal_change_ = true;
            break;
        case 0x10c: scroll_x1_ = value & 0x1ff; break;
        case 0x10e: scroll_y1_ = value & 0x1ff; break;
        case 0x110: scroll_x2_ = value & 0x3ff; break;
        case 0x112: scroll_y2_ = value & 0x3ff; break;
        case 0x114: scroll_x3_ = value & 0x7ff; break;
        case 0x116: scroll_y3_ = value & 0x7ff; break;
        case 0x118: stars_x1_ = value; break;
        case 0x11a: stars_y1_ = value; break;
        case 0x11c: stars_x2_ = value; break;
        case 0x11e: stars_y2_ = value; break;
        case 0x120: cps1_rowscrollstart_ = value & 0x7ff; break;
        case 0x122:
            flip_screen_ = (value & 0x8000) != 0;
            rowscroll_ena_ = (value & 1) != 0;
            break;
        case 0x180:
        case 0x182:
        case 0x184:
        case 0x186: sound_latch_ = uint8_t(value); break;
        case 0x188:
        case 0x18a:
        case 0x18c:
        case 0x18e: sound_latch2_ = uint8_t(value); break;
        default: break;
    }
    const CpsB& b = kCpsB[size_t(cps_b_index_)];
    if (dir == b.palctrl) {
        cps1_palcltr_ = value;
        pal_change_ = true;
        palette_dirty_.fill(1);
    }
    if (dir == b.mula) cps1_mula_ = value;
    if (dir == b.mulb) cps1_mulb_ = value;
    if (dir == b.layerctrl) cps1_layer_ = value;
    if (dir == b.pri_mask1 && pri_mask0_ != value) {
        calc_mask(value, 0);
        pri_mask0_ = value;
        mask_change_ = true;
    }
    if (dir == b.pri_mask2 && pri_mask1_ != value) {
        calc_mask(value, 1);
        pri_mask1_ = value;
        mask_change_ = true;
    }
    if (dir == b.pri_mask3 && pri_mask2_ != value) {
        calc_mask(value, 2);
        pri_mask2_ = value;
        mask_change_ = true;
    }
    if (dir == b.pri_mask4 && pri_mask3_ != value) {
        calc_mask(value, 3);
        pri_mask3_ = value;
        mask_change_ = true;
    }
}

void Cps1::test_buffers(uint32_t address) {
    if (address >= cps1_pal_ && address < cps1_pal_ + 0x1800) {
        pal_change_ = true;
        size_t index = (address - cps1_pal_) >> 1;
        if (index < palette_dirty_.size()) palette_dirty_[index] = 1;
    }
}

void Cps1::calc_mask(uint16_t mask, int index) {
    for (int pen = 0; pen < 16; pen++) trans_alt_[size_t(index)][size_t(pen)] = ((mask >> pen) & 1) == 0;
}

uint16_t Cps1::main_read(uint32_t address) {
    address &= 0xffffff;
    if (uses_qsound()) {
        if (address <= 0x17ffff) return rom_[(address >> 1) & 0xfffff];
        if (address >= 0x800000 && address <= 0x8001ff) return read_io(uint16_t(address & 0x1fe));
        if (address >= 0x900000 && address <= 0x92ffff) return vram_[(address & 0x3ffff) >> 1];
        if (address >= 0xf18000 && address <= 0xf19fff) {
            return uint16_t(0xff00 | qram1_[(address >> 1) & 0xfff]);
        }
        if (address == 0xf1c000 || address == 0xf1c002) return 0xff;
        if (address == 0xf1c006) return eeprom_.do_read();
        if (address >= 0xf1e000 && address <= 0xf1ffff) {
            return uint16_t(0xff00 | qram2_[(address >> 1) & 0xfff]);
        }
        if (address >= 0xff0000) return ram_[(address & 0xffff) >> 1];
        return 0xffff;
    }
    if (address <= 0x3fffff) return rom_[(address >> 1) & 0xfffff];
    if (address >= 0x800000 && address <= 0x8001ff) return read_io(uint16_t(address & 0x1fe));
    if (address >= 0x900000 && address <= 0x92ffff) return vram_[(address & 0x3ffff) >> 1];
    if (address >= 0xff0000) return ram_[(address & 0xffff) >> 1];
    return 0xffff;
}

void Cps1::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address >= 0x800000 && address <= 0x8001ff) {
        write_io(uint16_t(address & 0x1fe), value);
        return;
    }
    if (address >= 0x900000 && address <= 0x92ffff) {
        uint32_t offset = (address & 0x3ffff) >> 1;
        if (vram_[offset] != value) {
            vram_[offset] = value;
            test_buffers(address & 0x3ffff);
        }
        return;
    }
    if (uses_qsound()) {
        if (address >= 0xf18000 && address <= 0xf19fff) {
            qram1_[(address >> 1) & 0xfff] = uint8_t(value);
            return;
        }
        if (address == 0xf1c006) {
            eeprom_.di_write(uint8_t(value & 1));
            eeprom_.clk_write(uint8_t((value >> 6) & 1));
            eeprom_.cs_write(uint8_t((value >> 7) & 1));
            return;
        }
        if (address >= 0xf1e000 && address <= 0xf1ffff) {
            qram2_[(address >> 1) & 0xfff] = uint8_t(value);
            return;
        }
    }
    if (address >= 0xff0000) ram_[(address & 0xffff) >> 1] = value;
}

uint8_t Cps1::sound_read(uint16_t address) {
    if (address <= 0x7fff) return sound_rom_[address];
    if (address >= 0x8000 && address <= 0xbfff) return snd_bank_[sound_bank_ & 1][address & 0x3fff];
    if (address >= 0xd000 && address <= 0xd7ff) return sound_ram_[address & 0x7ff];
    if (address == 0xf001) return ym_.status();
    if (address == 0xf002) return oki_.read();
    if (address == 0xf008) return sound_latch_;
    if (address == 0xf00a) return sound_latch2_;
    return 0xff;
}

void Cps1::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0xd000 && address <= 0xd7ff) sound_ram_[address & 0x7ff] = value;
    else if (address == 0xf000) ym_.select_register(value);
    else if (address == 0xf001) ym_.write(value);
    else if (address == 0xf002) oki_.write(value);
    else if (address == 0xf004) sound_bank_ = value & 1;
    else if (address == 0xf006) oki_.set_pin7((value & 1) != 0);
}

uint8_t Cps1::qsound_z80_opcode(uint16_t address) {
    if (address <= 0x7fff) return qsnd_opcode_[address];
    return qsound_z80_read(address);
}

uint8_t Cps1::qsound_z80_read(uint16_t address) {
    if (address <= 0x7fff) return qsnd_data_[address];
    if (address >= 0x8000 && address <= 0xbfff) {
        return snd_bank_[sound_bank_ & 0xf][address & 0x3fff];
    }
    if (address >= 0xc000 && address <= 0xcfff) return qram1_[address & 0xfff];
    if (address == 0xd007) return qsound_.read();
    if (address >= 0xf000) return qram2_[address & 0xfff];
    return 0xff;
}

void Cps1::qsound_z80_write(uint16_t address, uint8_t value) {
    if (address >= 0xc000 && address <= 0xcfff) qram1_[address & 0xfff] = value;
    else if (address >= 0xd000 && address <= 0xd002) qsound_.write(uint8_t(address & 3), value);
    else if (address == 0xd003) sound_bank_ = value & 0xf;
    else if (address >= 0xf000) qram2_[address & 0xfff] = value;
}

int Cps1::gfx_bank(uint8_t type, uint16_t nchar) const {
    const BankMap& map = kBanks[size_t(nbank_)];
    int shift = 1;
    if (type == kScroll1) shift = 0;
    else if (type == kScroll3) shift = 3;
    uint32_t code = uint32_t(nchar) << shift;
    for (const BankRange& range : map.ranges) {
        if (range.type == 0) break;
        if (code < range.start || code > range.end) continue;
        if ((range.type & type) == 0) continue;
        uint32_t base = 0;
        for (int i = 0; i < range.num_bank; i++) base += map.lbank[size_t(i)];
        uint32_t size = map.lbank[size_t(range.num_bank)];
        uint32_t masked = size != 0 ? (code & (size - 1)) : code;
        return int((base + masked) >> shift);
    }
    return -1;
}

void Cps1::pal_calc() {
    int pos_buf = 0;
    for (int page = 0; page < 6; page++) {
        if ((cps1_palcltr_ & (1 << page)) != 0) {
            for (int offset = 0; offset < 0x200; offset++) {
                uint32_t idx = vram_index_from_byte(uint32_t(pos_buf) * 2u + cps1_pal_);
                uint16_t pal = vram_[idx];
                int bright = 0xf + int((pal >> 12) << 1);
                int r = (((pal >> 8) & 0xf) * 0x11 * bright) / 0x2d;
                int g = (((pal >> 4) & 0xf) * 0x11 * bright) / 0x2d;
                int b = (((pal >> 0) & 0xf) * 0x11 * bright) / 0x2d;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                palette_[size_t(page * 0x200 + offset)] =
                    0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
                pos_buf++;
            }
        } else if (pos_buf != 0) {
            pos_buf += 0x200;
        }
    }
    pal_change_ = false;
}

void Cps1::draw_tile(const GfxSet& gfx, std::vector<uint32_t>& dest, int dest_w, int dest_h, int x,
                     int y, int code, int color, bool flipx, bool flipy, const bool* trans_alt) {
    const uint8_t* pixels = gfx.element(code);
    int width = gfx.width();
    int height = gfx.height();
    for (int row = 0; row < height; row++) {
        int sy = flipy ? height - 1 - row : row;
        int dy = y + row;
        if (dy < 0 || dy >= dest_h) continue;
        for (int col = 0; col < width; col++) {
            int sx = flipx ? width - 1 - col : col;
            uint8_t pen = pixels[sy * width + sx];
            if (pen == 15) continue;
            if (trans_alt != nullptr && trans_alt[pen]) continue;
            int dx = x + col;
            if (dx < 0 || dx >= dest_w) continue;
            dest[size_t(dy * dest_w + dx)] = palette_[size_t((color + pen) & 0xbff)];
        }
    }
}

void Cps1::clear_tile(std::vector<uint32_t>& dest, int dest_w, int dest_h, int x, int y, int size) {
    for (int row = 0; row < size; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= dest_h) continue;
        for (int col = 0; col < size; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= dest_w) continue;
            dest[size_t(dy * dest_w + dx)] = 0;
        }
    }
}

void Cps1::blit_scrolled(const std::vector<uint32_t>& src, int src_w, int src_h, int scroll_x,
                         int scroll_y) {
    for (int y = 0; y < kWorkSize; y++) {
        int sy = y + scroll_y;
        sy %= src_h;
        if (sy < 0) sy += src_h;
        for (int x = 0; x < kWorkSize; x++) {
            int sx = x + scroll_x;
            sx %= src_w;
            if (sx < 0) sx += src_w;
            uint32_t pixel = src[size_t(sy * src_w + sx)];
            if (pixel != 0) composite_[size_t(y * kWorkSize + x)] = pixel;
        }
    }
}

void Cps1::blit_rowscroll() {
    std::array<uint16_t, 0x400> rows{};
    for (int i = 0; i < 0x400; i++) {
        uint32_t index = vram_index(cps1_rowscroll_ + cps1_rowscrollstart_ + uint32_t(i));
        rows[size_t(i)] = vram_[index];
    }
    for (int y = 0; y < kWorkSize; y++) {
        int sy = (y + int(scroll_y2_)) & 1023;
        int xoff = int(scroll_x2_) + int(rows[size_t(y & 0x3ff)]);
        for (int x = 0; x < kWorkSize; x++) {
            int sx = (x + xoff) & 1023;
            uint32_t pixel = scroll2_[size_t(sy * 1024 + sx)];
            if (pixel != 0) composite_[size_t(y * kWorkSize + x)] = pixel;
        }
    }
}

void Cps1::draw_sprites() {
    int last = 0xfe;
    for (int f = 0; f <= 0xfe; f++) {
        if (sprite_buffer_[size_t(f * 4 + 3)] == 0xff00) {
            last = f;
            break;
        }
    }
    for (int f = last; f >= 0; f--) {
        int nchar = gfx_bank(kSprites, sprite_buffer_[size_t(f * 4 + 2)]);
        if (nchar < 0) continue;
        uint16_t color = sprite_buffer_[size_t(f * 4 + 3)];
        int x = sprite_buffer_[size_t(f * 4 + 0)];
        int y = sprite_buffer_[size_t(f * 4 + 1)];
        int pal_base = (color & 0x1f) << 4;
        int rx = (color >> 8) & 0xf;
        int ry = (color >> 12) & 0xf;
        bool flipx = (color & 0x20) != 0;
        bool flipy = (color & 0x40) != 0;
        int dx = 16, dy = 16, dxx = -((rx + 1) << 4);
        if (flipx) {
            x += rx << 4;
            dx = -16;
            dxx = (rx + 1) << 4;
        }
        if (flipy) {
            y += ry << 4;
            dy = -16;
        }
        for (int yy = 0; yy <= ry; yy++) {
            for (int xx = 0; xx <= rx; xx++) {
                int code = nchar + xx + (yy << 4);
                const uint8_t* pixels = tiles16_.element(code);
                for (int row = 0; row < 16; row++) {
                    int sy = flipy ? 15 - row : row;
                    int dy_pix = (y + row) & 0x1ff;
                    for (int px = 0; px < 16; px++) {
                        int sx = flipx ? 15 - px : px;
                        uint8_t pen = pixels[sy * 16 + sx];
                        if (pen == 15) continue;
                        int dx_pix = (x + px) & 0x1ff;
                        composite_[size_t(dy_pix * kWorkSize + dx_pix)] =
                            palette_[size_t((pal_base + pen) & 0xbff)];
                    }
                }
                x += dx;
            }
            x += dxx;
            y += dy;
        }
    }
    if (sprites_pri_draw_) {
        blit_scrolled(priority_, 1024, 1024, scroll_pri_x_, scroll_pri_y_);
        sprites_pri_draw_ = false;
    }
}

void Cps1::draw_layer(int nlayer, bool sprite_next) {
    const CpsB& b = kCpsB[size_t(cps_b_index_)];
    if (nlayer == 0) {
        draw_sprites();
        return;
    }
    if (nlayer == 1 && (cps1_layer_ & b.mask_sc1) != 0) {
        std::fill(scroll1_.begin(), scroll1_.end(), 0);
        if (sprite_next) std::fill(priority_.begin(), priority_.end(), 0);
        for (int f = 0; f <= 0x6c7; f++) {
            int x = f % 56;
            int y = f / 56;
            int sx = x + ((scroll_x1_ & 0x1f8) / 8);
            int sy = y + ((scroll_y1_ & 0x1f8) / 8);
            int pos = (sy & 0x1f) + ((sx & 0x3f) << 5) + ((sy & 0x20) << 6);
            uint32_t address = cps1_scroll1_ + uint32_t(pos * 4);
            uint16_t atrib = vram_[vram_index_from_byte(address + 2)];
            int nchar = gfx_bank(kScroll1, vram_[vram_index_from_byte(address)]);
            if (nchar < 0) {
                clear_tile(scroll1_, 448, 248, x * 8, y * 8, 8);
                if (sprite_next) clear_tile(priority_, 1024, 1024, x * 8, y * 8, 8);
                continue;
            }
            bool flipx = (atrib & 0x20) != 0;
            bool flipy = (atrib & 0x40) != 0;
            int color = ((atrib & 0x1f) + 0x20) << 4;
            const GfxSet& gfx = ((pos & 0x20) != 0) ? chars1_ : chars0_;
            draw_tile(gfx, scroll1_, 448, 248, x * 8, y * 8, nchar, color, flipx, flipy, nullptr);
            if (sprite_next) {
                int pant = (atrib & 0x180) >> 7;
                draw_tile(gfx, priority_, 1024, 1024, x * 8, y * 8, nchar, color, flipx, flipy,
                          trans_alt_[size_t(pant)].data());
            }
        }
        if (sprite_next) {
            sprites_pri_draw_ = true;
            scroll_pri_x_ = scroll_x1_ & 7;
            scroll_pri_y_ = scroll_y1_ & 7;
        }
        blit_scrolled(scroll1_, 448, 248, scroll_x1_ & 7, scroll_y1_ & 7);
        return;
    }
    if (nlayer == 2 && (cps1_layer_ & b.mask_sc2) != 0) {
        std::fill(scroll2_.begin(), scroll2_.end(), 0);
        if (sprite_next) std::fill(priority_.begin(), priority_.end(), 0);
        for (int f = 0; f <= 0xfff; f++) {
            int x = f % 0x40;
            int y = f / 0x40;
            int pos = (y & 0xf) + ((x & 0x3f) << 4) + ((y & 0x30) << 6);
            uint32_t address = cps1_scroll2_ + uint32_t(pos * 4);
            uint16_t atrib = vram_[vram_index_from_byte(address + 2)];
            int nchar = gfx_bank(kScroll2, vram_[vram_index_from_byte(address)]);
            if (nchar < 0) {
                clear_tile(scroll2_, 1024, 1024, x * 16, y * 16, 16);
                if (sprite_next) clear_tile(priority_, 1024, 1024, x * 16, y * 16, 16);
                continue;
            }
            bool flipx = (atrib & 0x20) != 0;
            bool flipy = (atrib & 0x40) != 0;
            int color = ((atrib & 0x1f) + 0x40) << 4;
            draw_tile(tiles16_, scroll2_, 1024, 1024, x * 16, y * 16, nchar, color, flipx, flipy,
                      nullptr);
            if (sprite_next) {
                int pant = (atrib & 0x180) >> 7;
                draw_tile(tiles16_, priority_, 1024, 1024, x * 16, y * 16, nchar, color, flipx, flipy,
                          trans_alt_[size_t(pant)].data());
            }
        }
        if (sprite_next) {
            sprites_pri_draw_ = true;
            scroll_pri_x_ = scroll_x2_;
            scroll_pri_y_ = scroll_y2_;
        }
        if (!rowscroll_ena_) blit_scrolled(scroll2_, 1024, 1024, scroll_x2_, scroll_y2_);
        else blit_rowscroll();
        return;
    }
    if (nlayer == 3 && (cps1_layer_ & b.mask_sc3) != 0) {
        std::fill(scroll3_.begin(), scroll3_.end(), 0);
        if (sprite_next) std::fill(priority_.begin(), priority_.end(), 0);
        for (int f = 0; f <= 0x95; f++) {
            int x = f % 15;
            int y = f / 15;
            int sx = x + ((scroll_x3_ & 0x7e0) / 32);
            int sy = y + ((scroll_y3_ & 0x7e0) / 32);
            int pos = (sy & 0x07) + ((sx & 0x3f) << 3) + ((sy & 0x38) << 6);
            uint32_t address = cps1_scroll3_ + uint32_t(pos * 4);
            uint16_t atrib = vram_[vram_index_from_byte(address + 2)];
            int nchar = gfx_bank(kScroll3, vram_[vram_index_from_byte(address)]);
            if (nchar < 0) {
                clear_tile(scroll3_, 480, 320, x * 32, y * 32, 32);
                if (sprite_next) clear_tile(priority_, 1024, 1024, x * 32, y * 32, 32);
                continue;
            }
            bool flipx = (atrib & 0x20) != 0;
            bool flipy = (atrib & 0x40) != 0;
            int color = ((atrib & 0x1f) + 0x60) << 4;
            draw_tile(tiles32_, scroll3_, 480, 320, x * 32, y * 32, nchar, color, flipx, flipy,
                      nullptr);
            if (sprite_next) {
                int pant = (atrib & 0x180) >> 7;
                draw_tile(tiles32_, priority_, 1024, 1024, x * 32, y * 32, nchar, color, flipx, flipy,
                          trans_alt_[size_t(pant)].data());
            }
        }
        if (sprite_next) {
            sprites_pri_draw_ = true;
            scroll_pri_x_ = scroll_x3_ & 0x1f;
            scroll_pri_y_ = scroll_y3_ & 0x1f;
        }
        blit_scrolled(scroll3_, 480, 320, scroll_x3_ & 0x1f, scroll_y3_ & 0x1f);
    }
}

void Cps1::draw_stars() {
    for (int f = 0; f <= 0xfff; f++) {
        auto plot = [&](uint8_t col, uint16_t star_x, uint16_t star_y, int pal_base) {
            if ((col & 0x1f) == 0x0f) return;
            int x = (f / 256) * 32;
            int y = f % 256;
            x = (x - int(star_x) + (col & 0x1f)) & 0x1ff;
            y = (y - int(star_y)) & 0xff;
            int cnt = ((col & 0x80) != 0) ? int((cps1_frame_ / 16) % 15) : int((cps1_frame_ / 16) % 16);
            uint32_t color = palette_[size_t(pal_base + ((col & 0xe0) >> 1) + cnt)];
            composite_[size_t(y * kWorkSize + x)] = color;
        };
        plot(stars_[size_t(8 * f + 4)], stars_x2_, stars_y2_, 0xa00);
        plot(stars_[size_t(8 * f)], stars_x1_, stars_y1_, 0x800);
    }
}

void Cps1::copy_final() {
    auto src_at = [&](int x, int y) -> uint32_t {
        if (flip_screen_) {
            x = 64 + 383 - (x - 64);
            y = 16 + 223 - (y - 16);
        }
        return composite_[size_t(y * kWorkSize + x)];
    };
    if (!rotate_final()) {
        for (int y = 0; y < kScreenHeight; y++) {
            for (int x = 0; x < kScreenWidth; x++) {
                framebuffer_[size_t(y * kScreenWidth + x)] = src_at(64 + x, 16 + y);
            }
        }
        return;
    }
    // rot270: dest[sy, width-1-sx] of the 384x224 window, visible 224x384.
    for (int y = 0; y < kScreenWidth; y++) {
        for (int x = 0; x < kScreenHeight; x++) {
            framebuffer_[size_t(x * screen_width_ + (screen_width_ - 1 - y))] =
                src_at(64 + y, 16 + x);
        }
    }
}

void Cps1::update_video() {
    pal_calc();
    uint32_t fill = palette_[0xbff];
    if ((fill & 0xff000000u) == 0) fill = 0xff000000u;
    std::fill(composite_.begin(), composite_.end(), fill);
    color_dirty_.fill(0);
    int l0 = (cps1_layer_ >> 6) & 3;
    int l1 = (cps1_layer_ >> 8) & 3;
    int l2 = (cps1_layer_ >> 10) & 3;
    int l3 = (cps1_layer_ >> 12) & 3;
    const CpsB& b = kCpsB[size_t(cps_b_index_)];
    if (stars_enabled_ && (cps1_layer_ & b.mask_sc4) != 0) draw_stars();
    draw_layer(l0, l1 == 0);
    draw_layer(l1, l2 == 0);
    draw_layer(l2, l3 == 0);
    draw_layer(l3, false);
    copy_final();
}

void Cps1::cps1_gfx_decode(std::vector<uint8_t>& data) {
    uint32_t groups = uint32_t(data.size() / 4);
    for (uint32_t i = 0; i < groups; i++) {
        uint32_t src = uint32_t(data[i * 4]) | (uint32_t(data[i * 4 + 1]) << 8) |
                       (uint32_t(data[i * 4 + 2]) << 16) | (uint32_t(data[i * 4 + 3]) << 24);
        uint32_t dwval = 0;
        for (int j = 0; j < 8; j++) {
            uint8_t n = 0;
            uint32_t mask = (0x80808080u >> j) & src;
            if (mask & 0x000000ff) n |= 1;
            if (mask & 0x0000ff00) n |= 2;
            if (mask & 0x00ff0000) n |= 4;
            if (mask & 0xff000000) n |= 8;
            dwval |= uint32_t(n) << (j * 4);
        }
        data[i * 4] = uint8_t(dwval);
        data[i * 4 + 1] = uint8_t(dwval >> 8);
        data[i * 4 + 2] = uint8_t(dwval >> 16);
        data[i * 4 + 3] = uint8_t(dwval >> 24);
    }
}

void Cps1::poner_roms_word(const std::vector<uint8_t>& bytes, std::vector<uint16_t>& rom) {
    size_t words = std::min<size_t>(0xc0000, bytes.size() / 2);
    if (rom.size() < words) rom.resize(words);
    for (size_t i = 0; i < words; i++) {
        rom[i] = uint16_t((bytes[i * 2] << 8) | bytes[i * 2 + 1]);
    }
}

void Cps1::decode_graphics(const std::vector<uint8_t>& gfx, int chars, int tiles16, int tiles32) {
    chars0_.decode(char_layout(chars, false), gfx);
    chars1_.decode(char_layout(chars, true), gfx);
    tiles16_.decode(tile16_layout(tiles16), gfx);
    tiles32_.decode(tile32_layout(tiles32), gfx);
}

void Cps1::install_sound_rom(const std::vector<uint8_t>& sound) {
    sound_rom_.fill(0);
    size_t copy = std::min(sound.size(), size_t(0x8000));
    std::copy(sound.begin(), sound.begin() + int(copy), sound_rom_.begin());
    auto bank = [&](int index, size_t offset) {
        snd_bank_[size_t(index)].fill(0);
        if (offset + 0x4000 <= sound.size()) {
            std::copy(sound.begin() + int(offset), sound.begin() + int(offset + 0x4000),
                      snd_bank_[size_t(index)].begin());
        }
    };
    bank(0, 0x8000);
    bank(1, 0xc000);
    if (uses_qsound()) {
        bank(2, 0x10000);
        bank(3, 0x14000);
        bank(4, 0x18000);
        bank(5, 0x1c000);
    }
}

bool Cps1::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> cpu(0x180000, 0);
    std::vector<uint8_t> gfx(0x600000, 0);
    std::vector<uint8_t> sound;
    std::vector<uint8_t> oki;
    int chars = 0x8000, tiles16 = 0x4000, tiles32 = 0x1000;
    uint32_t gfx_size = 0x200000;

    switch (game_) {
        case Game::Ghouls:
            nbank_ = 0;
            cps_b_index_ = 0;
            dsw_a_ = 0xff;
            dsw_b_ = 0xfd;
            dsw_c_ = 0xff;
            chars = 0xc000;
            tiles16 = 0x6000;
            tiles32 = 0x1800;
            gfx_size = 0x300000;
            if (!load_16b(loader,
                          {{"dme_29.10h", 0x20000, 0, 0x166a58a2},
                           {"dme_30.10j", 0x20000, 1, 0x7ac8407a},
                           {"dme_27.9h", 0x20000, 0x40000, 0xf734b2be},
                           {"dme_28.9j", 0x20000, 0x40001, 0x03d3e714}},
                          cpu, error))
                return false;
            if (!load_raw(loader, {{"dm-17.7j", 0x80000, 0x80000, 0x3ea1b0f2}}, cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"26.10a", 0x10000, 0, 0x3692f6e5}}, sound, error))
                return false;
            if (!load_64b(loader,
                          {{"dm-05.3a", 0x80000, 0, 0x0ba9c0b0},
                           {"dm-07.3f", 0x80000, 2, 0x5d760ab9},
                           {"dm-06.3c", 0x80000, 4, 0x4ba90b59},
                           {"dm-08.3g", 0x80000, 6, 0x4bdee9de}},
                          gfx, error))
                return false;
            if (!load_64b_b(loader,
                            {{"09.4a", 0x10000, 0x200000, 0xae24bb19},
                             {"18.7a", 0x10000, 0x200001, 0xd34e271a},
                             {"13.4e", 0x10000, 0x200002, 0x3f70dd37},
                             {"22.7e", 0x10000, 0x200003, 0x7e69e2e6},
                             {"11.4c", 0x10000, 0x200004, 0x37c9b6c6},
                             {"20.7c", 0x10000, 0x200005, 0x2f1345b4},
                             {"15.4g", 0x10000, 0x200006, 0x3c2a212a},
                             {"24.7g", 0x10000, 0x200007, 0x889aac05},
                             {"10.4b", 0x10000, 0x280000, 0xbcc0f28c},
                             {"19.7b", 0x10000, 0x280001, 0x2a40166a},
                             {"14.4f", 0x10000, 0x280002, 0x20f85c03},
                             {"23.7f", 0x10000, 0x280003, 0x8426144b},
                             {"12.4d", 0x10000, 0x280004, 0xda088d61},
                             {"21.7d", 0x10000, 0x280005, 0x17e11df0},
                             {"16.4h", 0x10000, 0x280006, 0xf187ba1c},
                             {"25.7h", 0x10000, 0x280007, 0x29f79c78}},
                            gfx, error))
                return false;
            break;
        case Game::Ffight:
            nbank_ = 1;
            cps_b_index_ = 1;
            dsw_a_ = 0xff;
            dsw_b_ = 0xf4;
            dsw_c_ = 0x9f;
            if (!load_16b(loader,
                          {{"ff_36.11f|ffu_36.11f|ffe_36.11f", 0x20000, 0, 0xf9a5ce83},
                           {"ff_42.11h|ffu_42.11h|ffe_42.11h", 0x20000, 1, 0x65f11215},
                           {"ff_37.12f|ffu_37.12f|ffe_37.12f", 0x20000, 0x40000, 0xe1033784},
                           {"ffe_43.12h|ff_43.12h|ffu_43.12h", 0x20000, 0x40001, 0x995e968a}},
                          cpu, error))
                return false;
            if (!load_swap_word(loader, {{"ff-32m.8h", 0x80000, 0x80000, 0xc747696e}}, cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"ff_09.12b", 0x10000, 0, 0xb8367eb5}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"ff_18.11c", 0x20000, 0, 0x375c66e7},
                           {"ff_19.12c", 0x20000, 0x20000, 0x1ef137f9}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"ff-5m.7a", 0x80000, 0, 0x9c284108},
                           {"ff-7m.9a", 0x80000, 2, 0xa7584dfb},
                           {"ff-1m.3a", 0x80000, 4, 0x0b605e44},
                           {"ff-3m.5a", 0x80000, 6, 0x52291cd2}},
                          gfx, error))
                return false;
            gfx_size = 0x200000;
            break;
        case Game::Kod:
            nbank_ = 2;
            cps_b_index_ = 2;
            dsw_a_ = 0xff;
            dsw_b_ = 0xfc;
            dsw_c_ = 0x9f;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            if (!load_16b(loader,
                          {{"kde_30.11e", 0x20000, 0, 0xc7414fd4},
                           {"kde_37.11f", 0x20000, 1, 0xa5bf40d2},
                           {"kde_31.12e", 0x20000, 0x40000, 0x1fffc7bd},
                           {"kde_38.12f", 0x20000, 0x40001, 0x89e57a82},
                           {"kde_28.9e", 0x20000, 0x80000, 0x9367bcd9},
                           {"kde_35.9f", 0x20000, 0x80001, 0x4ca6a48a},
                           {"kde_29.10e", 0x20000, 0xc0000, 0x6a0ba878},
                           {"kde_36.10f", 0x20000, 0xc0001, 0xb509b39d}},
                          cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"kd_9.12a", 0x10000, 0, 0xbac6ec26}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"kd_18.11c", 0x20000, 0, 0x69ecb2c8},
                           {"kd_19.12c", 0x20000, 0x20000, 0x02d851c1}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"kd-5m.4a", 0x80000, 0, 0xe45b8701},
                           {"kd-7m.6a", 0x80000, 2, 0xa7750322},
                           {"kd-1m.3a", 0x80000, 4, 0x5f74bf78},
                           {"kd-3m.5a", 0x80000, 6, 0x5e5303bf},
                           {"kd-6m.4c", 0x80000, 0x200000, 0x113358f3},
                           {"kd-8m.6c", 0x80000, 0x200002, 0x38853c44},
                           {"kd-2m.3c", 0x80000, 0x200004, 0x9ef36604},
                           {"kd-4m.5c", 0x80000, 0x200006, 0x402b9b4f}},
                          gfx, error))
                return false;
            break;
        case Game::Sf2:
            nbank_ = 3;
            cps_b_index_ = 3;
            dsw_a_ = 0xff;
            dsw_b_ = 0xfc;
            dsw_c_ = 0x9f;
            chars = 0x18000;
            tiles16 = 0xc000;
            tiles32 = 0x3000;
            gfx_size = 0x600000;
            if (!load_16b(loader,
                          {{"sf2e_30g.11e", 0x20000, 0, 0xfe39ee33},
                           {"sf2e_37g.11f", 0x20000, 1, 0xfb92cd74},
                           {"sf2e_31g.12e", 0x20000, 0x40000, 0x69a0a301},
                           {"sf2e_38g.12f", 0x20000, 0x40001, 0x5e22db70},
                           {"sf2e_28g.9e", 0x20000, 0x80000, 0x8bf9f1e5},
                           {"sf2e_35g.9f", 0x20000, 0x80001, 0x626ef934},
                           {"sf2_29b.10e", 0x20000, 0xc0000, 0xbb4af315},
                           {"sf2_36b.10f", 0x20000, 0xc0001, 0xc02a13eb}},
                          cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"sf2_9.12a", 0x10000, 0, 0xa4823a1b}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"sf2_18.11c", 0x20000, 0, 0x7f162009},
                           {"sf2_19.12c", 0x20000, 0x20000, 0xbeade53f}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"sf2-5m.4a", 0x80000, 0, 0x22c9cc8e},
                           {"sf2-7m.6a", 0x80000, 2, 0x57213be8},
                           {"sf2-1m.3a", 0x80000, 4, 0xba529b4f},
                           {"sf2-3m.5a", 0x80000, 6, 0x4b1b33a8},
                           {"sf2-6m.4c", 0x80000, 0x200000, 0x2c7e2229},
                           {"sf2-8m.6c", 0x80000, 0x200002, 0xb5548f17},
                           {"sf2-2m.3c", 0x80000, 0x200004, 0x14b84312},
                           {"sf2-4m.5c", 0x80000, 0x200006, 0x5e9cd89a},
                           {"sf2-13m.4d", 0x80000, 0x400000, 0x994bfa58},
                           {"sf2-15m.6d", 0x80000, 0x400002, 0x3e66ad9d},
                           {"sf2-9m.3d", 0x80000, 0x400004, 0xc1befaa8},
                           {"sf2-11m.5d", 0x80000, 0x400006, 0x0627c831}},
                          gfx, error))
                return false;
            break;
        case Game::Strider:
            nbank_ = 4;
            cps_b_index_ = 0;
            dsw_a_ = 0xff;
            dsw_b_ = 0x8d;
            dsw_c_ = 0xff;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            stars_enabled_ = true;
            if (!load_16b(loader,
                          {{"30.11f", 0x20000, 0, 0xda997474},
                           {"35.11h", 0x20000, 1, 0x5463aaa3},
                           {"31.12f", 0x20000, 0x40000, 0xd20786db},
                           {"36.12h", 0x20000, 0x40001, 0x21aa2863}},
                          cpu, error))
                return false;
            if (!load_swap_word(loader, {{"st-14.8h", 0x80000, 0x80000, 0x9b3cfc08}}, cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"09.12b", 0x10000, 0, 0x2ed403bc}}, sound, error)) return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"18.11c", 0x20000, 0, 0x4386bc80},
                           {"19.12c", 0x20000, 0x20000, 0x444536d7}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"st-2.8a", 0x80000, 0, 0x4eee9aea},
                           {"st-11.10a", 0x80000, 2, 0x2d7f21e4},
                           {"st-5.4a", 0x80000, 4, 0x7705aa46},
                           {"st-9.6a", 0x80000, 6, 0x5b18b722},
                           {"st-1.7a", 0x80000, 0x200000, 0x005f000b},
                           {"st-10.9a", 0x80000, 0x200002, 0xb9441519},
                           {"st-4.3a", 0x80000, 0x200004, 0xb7d04e8b},
                           {"st-8.5a", 0x80000, 0x200006, 0x6b4713b4}},
                          gfx, error))
                return false;
            std::copy(gfx.begin(), gfx.begin() + 0x8000, stars_.begin());
            break;
        case Game::Wonder3:
            nbank_ = 5;
            cps_b_index_ = 4;
            dsw_a_ = 0xff;
            dsw_b_ = 0x9a;
            dsw_c_ = 0x99;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            if (!load_16b(loader,
                          {{"rte_30a.11f", 0x20000, 0, 0xef5b8b33},
                           {"rte_35a.11h", 0x20000, 1, 0x7d705529},
                           {"rte_31a.12f", 0x20000, 0x40000, 0x32835e5e},
                           {"rte_36a.12h", 0x20000, 0x40001, 0x7637975f},
                           {"rt_28a.9f", 0x20000, 0x80000, 0x054137c8},
                           {"rt_33a.9h", 0x20000, 0x80001, 0x7264cb1b},
                           {"rte_29a.10f", 0x20000, 0xc0000, 0xcddaa919},
                           {"rte_34a.10h", 0x20000, 0xc0001, 0xed52e7e5}},
                          cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"rt_9.12b", 0x10000, 0, 0xabfca165}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"rt_18.11c", 0x20000, 0, 0x26b211ab},
                           {"rt_19.12c", 0x20000, 0x20000, 0xdbe64ad0}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"rt-5m.7a", 0x80000, 0, 0x86aef804},
                           {"rt-7m.9a", 0x80000, 2, 0x4f057110},
                           {"rt-1m.3a", 0x80000, 4, 0x902489d0},
                           {"rt-3m.5a", 0x80000, 6, 0xe35ce720},
                           {"rt-6m.8a", 0x80000, 0x200000, 0x13cb0e7c},
                           {"rt-8m.10a", 0x80000, 0x200002, 0x1f055014},
                           {"rt-2m.4a", 0x80000, 0x200004, 0xe9a034f4},
                           {"rt-4m.6a", 0x80000, 0x200006, 0xdf0eea8b}},
                          gfx, error))
                return false;
            break;
        case Game::Captcomm:
            nbank_ = 6;
            cps_b_index_ = 5;
            dsw_a_ = 0xff;
            dsw_b_ = 0xf4;
            dsw_c_ = 0x9f;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            if (!load_swap_word(loader,
                                {{"cce_23d.8f", 0x80000, 0, 0x42c814c5},
                                 {"cc_22d.7f", 0x80000, 0x80000, 0x0fd34195}},
                                cpu, error))
                return false;
            if (!load_16b(loader,
                          {{"cc_24d.9e", 0x20000, 0x100000, 0x3a794f25},
                           {"cc_28d.9f", 0x20000, 0x100001, 0xfc3c2906}},
                          cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"cc_09.11a", 0x10000, 0, 0x698e8b58}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"cc_18.11c", 0x20000, 0, 0x6de2c2db},
                           {"cc_19.12c", 0x20000, 0x20000, 0xb99091ae}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"cc-5m.3a", 0x80000, 0, 0x7261d8ba},
                           {"cc-7m.5a", 0x80000, 2, 0x6a60f949},
                           {"cc-1m.4a", 0x80000, 4, 0x00637302},
                           {"cc-3m.6a", 0x80000, 6, 0xcc87cf61},
                           {"cc-6m.7a", 0x80000, 0x200000, 0x28718bed},
                           {"cc-8m.9a", 0x80000, 0x200002, 0xd4acc53a},
                           {"cc-2m.8a", 0x80000, 0x200004, 0x0c69f151},
                           {"cc-4m.10a", 0x80000, 0x200006, 0x1f9ebb97}},
                          gfx, error))
                return false;
            break;
        case Game::Knights:
            nbank_ = 7;
            cps_b_index_ = 6;
            dsw_a_ = 0xff;
            dsw_b_ = 0x7c;
            dsw_c_ = 0x9f;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            if (!load_swap_word(loader,
                                {{"kr_23e.8f", 0x80000, 0, 0x1b3997eb},
                                 {"kr_22.7f", 0x80000, 0x80000, 0xd0b671a9}},
                                cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"kr_09.11a", 0x10000, 0, 0x5e44d9ee}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"kr_18.11c", 0x20000, 0, 0xda69d15f},
                           {"kr_19.12c", 0x20000, 0x20000, 0xbfc654e9}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"kr-5m.3a", 0x80000, 0, 0x9e36c1a4},
                           {"kr-7m.5a", 0x80000, 2, 0xc5832cae},
                           {"kr-1m.4a", 0x80000, 4, 0xf095be2d},
                           {"kr-3m.6a", 0x80000, 6, 0x179dfd96},
                           {"kr-6m.7a", 0x80000, 0x200000, 0x1f4298d2},
                           {"kr-8m.9a", 0x80000, 0x200002, 0x37fa8751},
                           {"kr-2m.8a", 0x80000, 0x200004, 0x0200bc3d},
                           {"kr-4m.10a", 0x80000, 0x200006, 0x0bb2b4e7}},
                          gfx, error))
                return false;
            break;
        case Game::Sf2ce:
            nbank_ = 8;
            cps_b_index_ = 7;
            dsw_a_ = 0xff;
            dsw_b_ = 0xfc;
            dsw_c_ = 0x9f;
            chars = 0x18000;
            tiles16 = 0xc000;
            tiles32 = 0x3000;
            gfx_size = 0x600000;
            if (!load_swap_word(loader,
                                {{"s92e_23b.8f", 0x80000, 0, 0x0aaa1a3a},
                                 {"s92_22b.7f", 0x80000, 0x80000, 0x2bbe15ed},
                                 {"s92_21a.6f", 0x80000, 0x100000, 0x925a7877}},
                                cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"s92_09.11a", 0x10000, 0, 0x08f6b60e}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"s92_18.11c", 0x20000, 0, 0x7f162009},
                           {"s92_19.12c", 0x20000, 0x20000, 0xbeade53f}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"s92-1m.3a", 0x80000, 0, 0x03b0d852},
                           {"s92-3m.5a", 0x80000, 2, 0x840289ec},
                           {"s92-2m.4a", 0x80000, 4, 0xcdb5f027},
                           {"s92-4m.6a", 0x80000, 6, 0xe2799472},
                           {"s92-5m.7a", 0x80000, 0x200000, 0xba8a2761},
                           {"s92-7m.9a", 0x80000, 0x200002, 0xe584bfb5},
                           {"s92-6m.8a", 0x80000, 0x200004, 0x21e3f87d},
                           {"s92-8m.10a", 0x80000, 0x200006, 0xbefc47df},
                           {"s92-10m.3c", 0x80000, 0x400000, 0x960687d5},
                           {"s92-12m.5c", 0x80000, 0x400002, 0x978ecd18},
                           {"s92-11m.4c", 0x80000, 0x400004, 0xd6ec9a0a},
                           {"s92-13m.6c", 0x80000, 0x400006, 0xed2c67f6}},
                          gfx, error))
                return false;
            break;
        case Game::Dino:
            nbank_ = 9;
            cps_b_index_ = 8;
            dsw_a_ = dsw_b_ = dsw_c_ = 0xff;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            if (!load_swap_word(loader,
                                {{"cde_23a.8f", 0x80000, 0, 0x8f4e585e},
                                 {"cde_22a.7f", 0x80000, 0x80000, 0x9278aa12},
                                 {"cde_21a.6f", 0x80000, 0x100000, 0x66d23de2}},
                                cpu, error))
                return false;
            sound.assign(0x20000, 0);
            if (!load_raw(loader, {{"cd_q.5k", 0x20000, 0, 0x605fdb0b}}, sound, error)) return false;
            {
                std::vector<uint8_t> samples(0x200000, 0);
                if (!load_raw(loader,
                              {{"cd-q1.1k", 0x80000, 0, 0x60927775},
                               {"cd-q2.2k", 0x80000, 0x80000, 0x770f4c47},
                               {"cd-q3.3k", 0x80000, 0x100000, 0x2f273ffc},
                               {"cd-q4.4k", 0x80000, 0x180000, 0x2c67821d}},
                              samples, error))
                    return false;
                qsound_.set_rom(std::move(samples));
            }
            if (!load_64b(loader,
                          {{"cd-1m.3a", 0x80000, 0, 0x8da4f917},
                           {"cd-3m.5a", 0x80000, 2, 0x6c40f603},
                           {"cd-2m.4a", 0x80000, 4, 0x09c8fc2d},
                           {"cd-4m.6a", 0x80000, 6, 0x637ff38f},
                           {"cd-5m.7a", 0x80000, 0x200000, 0x470befee},
                           {"cd-7m.9a", 0x80000, 0x200002, 0x22bfb7a3},
                           {"cd-6m.8a", 0x80000, 0x200004, 0xe7599ac4},
                           {"cd-8m.10a", 0x80000, 0x200006, 0x211b4b15}},
                          gfx, error))
                return false;
            break;
        case Game::Punisher:
            nbank_ = 10;
            cps_b_index_ = 9;
            dsw_a_ = dsw_b_ = dsw_c_ = 0xff;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x400000;
            if (!load_16b(loader,
                          {{"pse_26.11e", 0x20000, 0, 0x389a99d2},
                           {"pse_30.11f", 0x20000, 1, 0x68fb06ac},
                           {"pse_27.12e", 0x20000, 0x40000, 0x3eb181c3},
                           {"pse_31.12f", 0x20000, 0x40001, 0x37108e7b},
                           {"pse_24.9e", 0x20000, 0x80000, 0x0f434414},
                           {"pse_28.9f", 0x20000, 0x80001, 0xb732345d},
                           {"pse_25.10e", 0x20000, 0xc0000, 0xb77102e2},
                           {"pse_29.10f", 0x20000, 0xc0001, 0xec037bce}},
                          cpu, error))
                return false;
            if (!load_swap_word(loader, {{"ps_21.6f", 0x80000, 0x100000, 0x8affa5a9}}, cpu, error))
                return false;
            sound.assign(0x20000, 0);
            if (!load_raw(loader, {{"ps_q.5k", 0x20000, 0, 0x49ff4446}}, sound, error)) return false;
            {
                std::vector<uint8_t> samples(0x200000, 0);
                if (!load_raw(loader,
                              {{"ps-q1.1k", 0x80000, 0, 0x31fd8726},
                               {"ps-q2.2k", 0x80000, 0x80000, 0x980a9eef},
                               {"ps-q3.3k", 0x80000, 0x100000, 0x0dd44491},
                               {"ps-q4.4k", 0x80000, 0x180000, 0xbed42f03}},
                              samples, error))
                    return false;
                qsound_.set_rom(std::move(samples));
            }
            if (!load_64b(loader,
                          {{"ps-1m.3a", 0x80000, 0, 0x77b7ccab},
                           {"ps-3m.5a", 0x80000, 2, 0x0122720b},
                           {"ps-2m.4a", 0x80000, 4, 0x64fa58d4},
                           {"ps-4m.6a", 0x80000, 6, 0x60da42c8},
                           {"ps-5m.7a", 0x80000, 0x200000, 0xc54ea839},
                           {"ps-7m.9a", 0x80000, 0x200002, 0x04c5acbd},
                           {"ps-6m.8a", 0x80000, 0x200004, 0xa544f4cc},
                           {"ps-8m.10a", 0x80000, 0x200006, 0x8f02f436}},
                          gfx, error))
                return false;
            break;
        case Game::Willow:
            nbank_ = 11;
            cps_b_index_ = 10;
            dsw_a_ = 0xff;
            dsw_b_ = 0xff;
            dsw_c_ = 0xfa;
            chars = 0x10000;
            tiles16 = 0x8000;
            tiles32 = 0x2000;
            gfx_size = 0x300000;
            if (!load_16b(loader,
                          {{"wle_30.11f", 0x20000, 0, 0x15372aa2},
                           {"wle_35.11h", 0x20000, 1, 0x2e64623b},
                           {"wlu_31.12f", 0x20000, 0x40000, 0x0eb48a83},
                           {"wlu_36.12h", 0x20000, 0x40001, 0x36100209}},
                          cpu, error))
                return false;
            if (!load_swap_word(loader, {{"wlm-32.8h", 0x80000, 0x80000, 0xdfd9f643}}, cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"wl_09.12b", 0x10000, 0, 0xf6b3d060}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"wl_18.11c", 0x20000, 0, 0xbde23d4d},
                           {"wl_19.12c", 0x20000, 0x20000, 0x683898f5}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"wlm-7.7a", 0x80000, 0, 0xafa74b73},
                           {"wlm-5.9a", 0x80000, 2, 0x12a0dc0b},
                           {"wlm-3.3a", 0x80000, 4, 0xc6f2abce},
                           {"wlm-1.5a", 0x80000, 6, 0x4aa4c6d3}},
                          gfx, error))
                return false;
            if (!load_64b_b(loader,
                            {{"wl_24.7d", 0x20000, 0x200000, 0x6f0adee5},
                             {"wl_14.7c", 0x20000, 0x200001, 0x9cf3027d},
                             {"wl_26.9d", 0x20000, 0x200002, 0xf09c8ecf},
                             {"wl_16.9c", 0x20000, 0x200003, 0xe35407aa},
                             {"wl_20.3d", 0x20000, 0x200004, 0x84992350},
                             {"wl_10.3c", 0x20000, 0x200005, 0xb87b5a36},
                             {"wl_22.5d", 0x20000, 0x200006, 0xfd3f89f0},
                             {"wl_12.5c", 0x20000, 0x200007, 0x7da49d69}},
                            gfx, error))
                return false;
            break;
        case Game::Ca1941:
            nbank_ = 12;
            cps_b_index_ = 11;
            dsw_a_ = 0xff;
            dsw_b_ = 0xfc;
            dsw_c_ = 0x9f;
            gfx_size = 0x200000;
            if (!load_16b(loader,
                          {{"41em_30.11f", 0x20000, 0, 0x4249ec61},
                           {"41em_35.11h", 0x20000, 1, 0xddbee5eb},
                           {"41em_31.12f", 0x20000, 0x40000, 0x584e88e5},
                           {"41em_36.12h", 0x20000, 0x40001, 0x3cfc31d0}},
                          cpu, error))
                return false;
            if (!load_swap_word(loader, {{"41-32m.8h", 0x80000, 0x80000, 0x4e9648ca}}, cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"41_9.12b", 0x10000, 0, 0x0f9d8527}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"41_18.11c", 0x20000, 0, 0xd1f15aeb},
                           {"41_19.12c", 0x20000, 0x20000, 0x15aec3a6}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"41-5m.7a", 0x80000, 0, 0x01d1cb11},
                           {"41-7m.9a", 0x80000, 2, 0xaeaa3509},
                           {"41-1m.3a", 0x80000, 4, 0xff77985a},
                           {"41-3m.5a", 0x80000, 6, 0x983be58f}},
                          gfx, error))
                return false;
            break;
        case Game::Nemo:
            nbank_ = 13;
            cps_b_index_ = 12;
            dsw_a_ = 0xff;
            dsw_b_ = 0xfc;
            dsw_c_ = 0x9f;
            gfx_size = 0x200000;
            if (!load_16b(loader,
                          {{"nme_30a.11f", 0x20000, 0, 0xd2c03e56},
                           {"nme_35a.11h", 0x20000, 1, 0x5fd31661},
                           {"nme_31a.12f", 0x20000, 0x40000, 0xb2bd4f6f},
                           {"nme_36a.12h", 0x20000, 0x40001, 0xee9450e3}},
                          cpu, error))
                return false;
            if (!load_swap_word(loader, {{"nm-32m.8h", 0x80000, 0x80000, 0xd6d1add3}}, cpu, error))
                return false;
            sound.assign(0x10000, 0);
            if (!load_raw(loader, {{"nme_09.12b", 0x10000, 0, 0x0f4b0581}}, sound, error))
                return false;
            oki.assign(0x40000, 0);
            if (!load_raw(loader,
                          {{"nme_18.11c", 0x20000, 0, 0xbab333d4},
                           {"nme_19.12c", 0x20000, 0x20000, 0x2650a0a8}},
                          oki, error))
                return false;
            if (!load_64b(loader,
                          {{"nm-5m.7a", 0x80000, 0, 0x487b8747},
                           {"nm-7m.9a", 0x80000, 2, 0x203dc8c6},
                           {"nm-1m.3a", 0x80000, 4, 0x9e878024},
                           {"nm-3m.5a", 0x80000, 6, 0xbb01e6b6}},
                          gfx, error))
                return false;
            break;
    }

    poner_roms_word(cpu, rom_);
    install_sound_rom(sound);
    if (uses_qsound()) {
        if (game_ == Game::Dino) {
            kabuki_cps1_decode(sound, qsnd_opcode_, qsnd_data_, 0x76543210, 0x24601357, 0x4343, 0x43);
        } else {
            kabuki_cps1_decode(sound, qsnd_opcode_, qsnd_data_, 0x67452103, 0x75316024, 0x2222, 0x22);
        }
    }
    if (!oki.empty()) oki_.set_rom(std::move(oki));
    if (gfx.size() < gfx_size) gfx.resize(gfx_size, 0);
    if (gfx.size() > gfx_size) gfx.resize(gfx_size);
    cps1_gfx_decode(gfx);
    decode_graphics(gfx, chars, tiles16, tiles32);
    warnings_ = loader.warnings();
    return true;
}

}  // namespace dsp
