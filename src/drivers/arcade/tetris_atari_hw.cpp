#include "drivers/arcade/tetris_atari_hw.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const RomEntry kMainRom{"136066-1100.45f", 0x10000, 0, 0x2acbdb09};
const RomEntry kCharRom{"136066-1101.35a", 0x10000, 0, 0x84a1939f};

// pc_x / pc_y from tetris_atari_hw.pas. 4bpp nibble-interleaved bit planes.
inline uint8_t pal3bit(uint8_t bits) {
    bits &= 7;
    return uint8_t((bits << 5) | (bits << 2) | (bits >> 1));
}
inline uint8_t pal2bit(uint8_t bits) {
    bits &= 3;
    return uint8_t((bits << 6) | (bits << 4) | (bits << 2) | bits);
}

}  // namespace

AtariTetris::AtariTetris()
    : cpu_(kCpuClock),
      slapstic_(101, nullptr),
      pokey0_(kCpuClock),
      pokey1_(kCpuClock) {
    screen_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0);
    shown_palette_.fill(0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t address) { return main_read(address); },
                             [this](uint16_t address, uint8_t value) {
                                 main_write(address, value);
                             });
    cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    pokey0_.set_allpot_handler([this](uint8_t) { return uint8_t(in0_ | dsw_a_); });
    pokey1_.set_allpot_handler([this](uint8_t) { return in1_; });
}

bool AtariTetris::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // Main program: 64 KB split into bank 0 (0x0000-0x3fff), bank 1
    // (0x4000-0x7fff) and the fixed 32 KB tail (0x8000-0xffff).
    // Character graphics: 0x800 8x8 tiles, 4bpp, a nibble per pixel.
    // Both are read in place when the board maps them.
    auto load = [&](const RomEntry& entry, std::vector<uint8_t>& owned, const uint8_t*& rom) {
        rom = loader.map(entry);
        if (rom != nullptr) {
            owned.clear();
            owned.shrink_to_fit();
            return true;
        }
        owned.assign(0x10000, 0);
        if (!loader.load({entry}, owned, error)) return false;
        rom = owned.data();
        return true;
    };
    if (!load(kMainRom, main_rom_owned_, main_rom_)) return false;
    if (!load(kCharRom, char_rom_owned_, char_rom_)) return false;

    nv_ram_.fill(0xff);
    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);

    reset();
    return true;
}

void AtariTetris::reset() {
    slapstic_.reset();
    rom_bank_ = uint8_t(slapstic_.current_bank() & 1);
    cpu_.reset();
    pokey0_.reset();
    pokey1_.reset();
    pokey_pending_ = 0;
    in0_ = 0x40;
    in1_ = 0;
    nvram_write_enable_ = false;
    std::fill(screen_.begin(), screen_.end(), uint8_t(0));
    shown_palette_.fill(0xff000000u);
    framebuffer_stale_ = true;
    audio_accumulator_ = 0;
    audio_.clear();
}

const uint32_t* AtariTetris::framebuffer() const {
    if (framebuffer_stale_) {
        framebuffer_.resize(screen_.size());
        for (size_t i = 0; i < screen_.size(); ++i) framebuffer_[i] = shown_palette_[screen_[i]];
        framebuffer_stale_ = false;
    }
    return framebuffer_.data();
}

void AtariTetris::set_palette(int index, uint8_t value) {
    const uint8_t r = pal3bit(uint8_t(value >> 5));
    const uint8_t g = pal3bit(uint8_t((value >> 2) & 7));
    const uint8_t b = pal2bit(uint8_t(value & 3));
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

uint8_t AtariTetris::main_read(uint16_t address) {
    if (address <= 0x1fff) return ram_[address];
    if (address >= 0x2000 && address <= 0x23ff) return buffer_paleta_[address & 0xff];
    if (address >= 0x2400 && address <= 0x27ff) return nv_ram_[address & 0x1ff];
    if (address >= 0x2800 && address <= 0x2bff) {
        sync_pokeys();
        if ((address & 0x1f) < 0x10) return pokey0_.read(address & 0x0f);
        return pokey1_.read(address & 0x0f);
    }
    if (address >= 0x4000 && address <= 0x5fff)
        return main_rom_[size_t(rom_bank_ & 1) * 0x4000 + (address & 0x3fff)];
    if (address >= 0x6000 && address <= 0x7fff) {
        const uint8_t result = main_rom_[size_t(rom_bank_ & 1) * 0x4000 + (address & 0x3fff)];
        rom_bank_ = uint8_t(slapstic_.tweak(address & 0x1fff) & 1);
        return result;
    }
    if (address >= 0x8000) return main_rom_[address];
    return 0;
}

void AtariTetris::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        ram_[address] = value;
        return;
    }
    if (address >= 0x1000 && address <= 0x1fff) {
        ram_[address] = value;  // tile attribute memory, redrawn each frame
        return;
    }
    if (address >= 0x2000 && address <= 0x23ff) {
        const int index = address & 0xff;
        if (buffer_paleta_[size_t(index)] != value) {
            buffer_paleta_[size_t(index)] = value;
            set_palette(index, value);
        }
        return;
    }
    if (address >= 0x2400 && address <= 0x27ff) {
        if (nvram_write_enable_) nv_ram_[address & 0x1ff] = value;
        nvram_write_enable_ = false;
        return;
    }
    if (address >= 0x2800 && address <= 0x2bff) {
        sync_pokeys();
        if ((address & 0x1f) < 0x10) pokey0_.write(address & 0x0f, value);
        else pokey1_.write(address & 0x0f, value);
        return;
    }
    if (address >= 0x3000 && address <= 0x33ff) return;  // watchdog
    if (address >= 0x3400 && address <= 0x37ff) {
        nvram_write_enable_ = true;
        return;
    }
    if (address >= 0x3800 && address <= 0x3bff) {
        cpu_.set_irq(IrqLine::Clear);
        return;
    }
    if (address >= 0x3c00 && address <= 0x3fff) return;  // coin counter
    // $4000-$ffff is ROM, writes ignored.
}

// The POKEYs run in batches: up to the CPU's next access to them, or to the
// end of the instruction on which a sound sample is due (they have no
// interrupt line wired here, so nothing else sees them in between).
void AtariTetris::sync_pokeys() {
    if (pokey_pending_ > 0) {
        pokey0_.run(pokey_pending_);
        pokey1_.run(pokey_pending_);
        pokey_pending_ = 0;
    }
}

void AtariTetris::on_sound_cycles(int cycles) {
    pokey_pending_ += cycles;
    audio_accumulator_ += int64_t(cycles) * sample_rate_;
    if (audio_accumulator_ >= int64_t(kCpuClock)) sync_pokeys();
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        const int32_t sample = pokey0_.update() + pokey1_.update();
        audio_.push_back(int16_t(std::clamp<int32_t>(sample, -32768, 32767)));
    }
}

void AtariTetris::update_video() {
    // The 64x32 tilemap (opaque), drawn a whole row at a time: 42 columns of
    // 8x8 characters fill the 336 pixels, 30 rows the 240 lines. A character
    // row is 4 bytes of the ROM, two pixels each (high nibble first).
    shown_palette_ = palette_;
    framebuffer_stale_ = true;
    constexpr int kColumns = kScreenWidth / 8;
    for (int dest_y = 0; dest_y < kScreenHeight; ++dest_y) {
        const int row = dest_y & 7;
        const uint8_t* tiles = ram_.data() + 0x1000 + size_t(dest_y >> 3) * 64 * 2;
        uint8_t* dst = screen_.data() + size_t(dest_y) * kScreenWidth;
        for (int x = 0; x < kColumns; ++x, dst += 8) {
            const uint8_t atrib = tiles[x * 2 + 1];
            const unsigned nchar = unsigned(tiles[x * 2]) | (unsigned(atrib & 7) << 8);
            const uint8_t base = uint8_t((atrib >> 4) << 4);
            const uint8_t* bits = char_rom_ + nchar * 32 + row * 4;
            for (int i = 0; i < 4; ++i) {
                dst[i * 2] = uint8_t(base | (bits[i] >> 4));
                dst[i * 2 + 1] = uint8_t(base | (bits[i] & 0x0f));
            }
        }
    }
}

void AtariTetris::run_frame() {
    const int main_cycles = int(double(kCpuClock) / kFramesPerSecond / kScanlines + 0.5);
    for (int line = 0; line < kScanlines; line++) {
        switch (line) {
            case 0: in0_ = uint8_t(in0_ | 0x40); break;
            case 48:
            case 112:
            case 176: cpu_.set_irq(IrqLine::Assert); break;
            case 240:
                if (draw_enabled_) update_video();
                in0_ = uint8_t(in0_ & ~0x40);
                cpu_.set_irq(IrqLine::Assert);
                break;
            default: break;
        }
        cpu_.run(main_cycles);
    }
}

void AtariTetris::set_inputs(const MachineInputs& inputs) {
    in0_ = 0x40;
    if (inputs.coin1) in0_ = uint8_t(in0_ | 0x02);
    if (inputs.coin2) in0_ = uint8_t(in0_ | 0x01);

    in1_ = 0;
    if (inputs.player1.button1) in1_ |= 0x01;
    if (inputs.player1.down) in1_ |= 0x02;
    if (inputs.player1.right) in1_ |= 0x04;
    if (inputs.player1.left) in1_ |= 0x08;
    if (inputs.player2.button1) in1_ |= 0x10;
    if (inputs.player2.down) in1_ |= 0x20;
    if (inputs.player2.right) in1_ |= 0x40;
    if (inputs.player2.left) in1_ |= 0x80;
}

void AtariTetris::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
}

void AtariTetris::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp